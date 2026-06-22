// HttpServer — Http Server implementation.

#include "network/http/HttpServer.h"

#include <event2/keyvalq_struct.h>
#include <event2/util.h>
#include <unistd.h>

#include <memory>

#include "event2/http.h"
#include "event2/http_struct.h"
#include "event2/thread.h"
#include "network/http/HttpCommon.h"
#include "network/http/HttpServerThread.h"
#include "network/http/HttpServerThreadPool.h"
#include "util/CipherUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/StringUtil.h"
#include "util/UuidUtil.h"

namespace chrono = std::chrono;

namespace cosmo::network::http {

HttpServer::HttpServer() {}

HttpServer::~HttpServer() {
    UnInitialize();
}

void HttpServer::ComGenCb(struct evhttp_request* req, void* arg) {
    if (!req) {
        LOG_WARN("{}", "ComGenCb Get Null Request");
        return;
    }
    auto uri = evhttp_request_get_uri(req);
    if (strncmp(uri, "/v1/cwai/aihost/", strlen("/v1/cwai/aihost/")) == 0) {
        if (auto httpServer = static_cast<HttpServer*>(arg))
            httpServer->InsertHttpReqMsg(req);
    } else if (strncmp(uri, "/logs/", strlen("/logs/")) == 0) {
        if (auto httpServer = static_cast<HttpServer*>(arg))
            httpServer->InsertHttpOctetMsg(req);
    } else {
        // BAD REQUEST
        if (auto httpServer = static_cast<HttpServer*>(arg))
            httpServer->InsertHttpReqMsg(req);
    }
}

void HttpServer::InsertHttpReqMsg(struct evhttp_request* req) {
    auto task = std::make_unique<HttpReqTask>(req, chrono::steady_clock::now(), evhttp_request_get_uri(req));
    cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpReq),
                           std::unique_ptr<cosmo::MsgTask>(task.release()));
    LOG_INFO("{}", "PutMsg To Http Pool");
    thread_pool_.PutMsg(std::move(msg));
    LOG_INFO("{}", "PutMsg To Http Pool Ok");
}

void HttpServer::InsertHttpOctetMsg(struct evhttp_request* req) {
    auto task = std::make_unique<HttpReqTask>(req, chrono::steady_clock::now(), evhttp_request_get_uri(req));
    cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpOctetReq),
                           std::unique_ptr<cosmo::MsgTask>(task.release()));
    LOG_INFO("{}", "PutOctetMsg To Http Pool");
    thread_pool_.PutMsg(std::move(msg));
    LOG_INFO("{}", "PutOctetMsg To Http Pool Ok");
}

void HttpServer::SetAppInfo(const std::string& app_key, const std::string& app_secret) {
    app_key_    = app_key;
    app_secret_ = app_secret;
}

bool HttpServer::Initialize(const std::string& host_ip, int port, DispatcherFactory factory,
                            HttpServerCallbacks callbacks) {
    dispatcher_factory_ = std::move(factory);
    callbacks_          = std::move(callbacks);
    if (!(event_base_ = event_base_new())) {
        LOG_ERRO("{}", "event_base_new return NULL");
        return false;
    }

    if (!(event_http_ = evhttp_new(event_base_))) {
        LOG_ERRO("{}", "evhttp_new return NULL");
        return false;
    }

    // 5ms tick: ensures max response latency is bounded, while blocking to yield CPU when idle
    if (!tick_event_) {
        tick_event_ = event_new(
            event_base_, -1, EV_PERSIST,
            [](evutil_socket_t /*fd*/, short /*events*/, void* /*arg*/) {
                // Only used to periodically wake the event loop so dispatchMsg() can process the response
                // queue, avoiding busy-waiting.
            },
            this);
        if (!tick_event_) {
            LOG_ERRO("{}", "event_new tick failed");
            return false;
        }
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 5000;  // 5ms
        if (event_add(tick_event_, &tv) != 0) {
            LOG_ERRO("{}", "event_add tick failed");
            event_free(tick_event_);
            tick_event_ = nullptr;
            return false;
        }
    }

    // IMPORTANT: libevent caches the entire request body in memory by default.
    // For multipart uploads, allowing oversized bodies can easily trigger OOM.
    // The frontend allows 500MB file uploads (bulk face import zip), plus multipart encoding overhead,
    // so the limit must be >= 500MB. Otherwise libevent closes the connection without HTTP response,
    // causing nginx to return 502 Bad Gateway.
    evhttp_set_max_body_size(event_http_, 600LL * 1024 * 1024);  // 600MB

    // generic cb
    evhttp_set_gencb(event_http_, ComGenCb, this);

    // allow post method
    evhttp_set_allowed_methods(event_http_, EVHTTP_REQ_POST | EVHTTP_REQ_GET | EVHTTP_REQ_HEAD);

    int nRet = evhttp_bind_socket(event_http_, host_ip.data(), static_cast<uint16_t>(port));
    if (nRet != 0) {
        LOG_ERRO("evhttp_bind_socket ip:[{}], port:[{}], ret:[{}]", host_ip, port, nRet);
        return false;
    }

    constexpr int kThreadNum = 4;
    if (!thread_pool_.Initialize(kThreadNum, this, dispatcher_factory_)) {
        return false;
    }

    is_running_ = true;

    LOG_INFO("libevent http server ip[{}]-port[{}] start success", host_ip, port);

    return true;
}

void HttpServer::UnInitialize() {
    if (is_running_) {
        is_running_ = false;

        if (event_base_)
            event_base_loopexit(event_base_, nullptr);

        if (tick_event_) {
            event_free(tick_event_);
            tick_event_ = nullptr;
        }

        if (event_http_) {
            evhttp_free(event_http_);
            event_http_ = nullptr;
        }

        if (event_base_) {
            event_base_free(event_base_);
            event_base_ = nullptr;
        }

        thread_pool_.Uninitialize();
    }
}

int HttpServer::DispatchJsonMsg(HttpAckTask* task) {
    struct evhttp_request* ev_http_req = task->request;
    if (ev_http_req == nullptr) {
        LOG_ERRO("{}", "ev_http_req is nullptr!");
        return 0;
    }

    if (!AddHttpHeader(ev_http_req, task->request_id)) {
        return 0;
    }

    // NOTE: Cannot reuse a static evbuffer. Otherwise each evbuffer_add accumulates,
    // causing memory growth in high-frequency response scenarios like chunked uploads.
    struct evbuffer* evRes = evbuffer_new();
    if (!evRes) {
        LOG_ERRO("{}", "evbuffer_new failed");
        return 0;
    }
    if (task->response.empty()) {
        LOG_ERRO("{}", "task->response is empty");
    }
    evbuffer_add(evRes, task->response.c_str(), task->response.size());

    auto& codeMsg   = GetHttpResCodeMsg();
    auto it         = codeMsg.find(task->http_ack_code);
    const char* msg = (it != codeMsg.end()) ? it->second.c_str() : "UNKNOWN";
    evhttp_send_reply(ev_http_req, task->http_ack_code, msg, evRes);
    evbuffer_free(evRes);
    return 1;
}

int HttpServer::DispatchFileMsg(HttpAckTask* task) {
    struct evhttp_request* ev_http_req = task->request;
    if (ev_http_req == nullptr) {
        LOG_ERRO("{}", "ev_http_req is nullptr!");
        return 0;
    }

    FILE* fp = fopen(task->file_path.c_str(), "rb");
    if (!fp) {
        struct evbuffer* buf = evbuffer_new();
        evbuffer_add_printf(buf, "File not found: %s", task->file_name.c_str());
        evhttp_send_reply(ev_http_req, HTTP_NOTFOUND, "File Not Found", buf);
        evbuffer_free(buf);
        LOG_ERRO("File Path:{} Name:{} Not Found", task->file_path, task->file_name);
        return 0;
    }

    fseek(fp, 0L, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    if (!AddHttpOctetHeader(ev_http_req, task->request_id, task->file_name, fsize)) {
        fclose(fp);
        return 0;
    }

    evhttp_send_reply_start(ev_http_req, HTTP_OK, "OK");

    char buf[8192];
    size_t nread = 0;
    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        struct evbuffer* chunk = evbuffer_new();
        evbuffer_add(chunk, buf, nread);
        evhttp_send_reply_chunk(ev_http_req, chunk);
        evbuffer_free(chunk);
    }
    evhttp_send_reply_end(ev_http_req);
    fclose(fp);
    return 1;
}

void HttpServer::DispatchMsg() {
    if (!event_base_) {
        LOG_ERRO("{}", "DispatchMsg: event base is null");
        return;
    }
    while (is_running_) {
        // Block once: woken by network events or 5ms tick
        int rc = event_base_loop(event_base_, EVLOOP_ONCE);
        if (rc < 0) {
            LOG_ERRO("{}", "event_base_loop failed");
            break;
        }

        int handle_cnt = HandleRespMsgList();
        if (handle_cnt < 0) {
            LOG_WARN("{}", "QUIT");
            break;
        }
    }
}

int HttpServer::Put(cosmo::MsgEnvelope&& msg) {
    std::lock_guard<std::mutex> lck(msg_mutex_);
    msg_list_.push_back(std::move(msg));
    return static_cast<int>(msg_list_.size());
}

int HttpServer::HandleRespMsgList() {
    if (msg_list_.empty()) {
        return 0;
    }

    cosmo::MsgEnvelopeList lst_msg;
    {
        std::lock_guard<std::mutex> lck(msg_mutex_);
        lst_msg.swap(msg_list_);
    }

    int handle_cnt = 0;

    for (auto& it : lst_msg) {
        LOG_INFO("Handle:{}", it.GetMsgId());
        auto acl_task = it.ReleaseData();

        auto task = static_cast<HttpAckTask*>(acl_task.get());
        if (task != nullptr) {
            auto msgId = static_cast<InnerMsgId>(it.GetMsgId());
            switch (msgId) {
                case InnerMsgId::kHttpOctetAck:
                    handle_cnt += DispatchFileMsg(task);
                    break;
                case InnerMsgId::kHttpQuitAck:
                    return -1;
                default:
                    handle_cnt += DispatchJsonMsg(task);
                    break;
            }
        } else {
            LOG_ERRO("{}", "task is nullptr!");
            return 0;
        }
    }

    return handle_cnt;
}

bool HttpServer::AddCommonHeaders(struct evkeyvalq* ev_header, const std::string& req_id) {
    evhttp_add_header(ev_header, "Server", "CWAI");
    evhttp_add_header(ev_header, "RequestId", req_id.c_str());
    evhttp_add_header(ev_header, "AppKey", app_key_.c_str());

    std::string Nonce = cosmo::util::GenerateUUID();
    evhttp_add_header(ev_header, "Nonce", Nonce.c_str());

    auto now      = std::chrono::system_clock::now().time_since_epoch();
    auto cur_time = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    char cur_time_buf[32] = {0};
    snprintf(cur_time_buf, sizeof(cur_time_buf), "%013ld", cur_time);
    evhttp_add_header(ev_header, "CurTime", cur_time_buf);

    std::string sha_str   = Nonce + app_secret_ + cur_time_buf;
    std::string check_sum = cosmo::util::Sha1(sha_str);
    evhttp_add_header(ev_header, "CheckSum", check_sum.c_str());
    return true;
}

bool HttpServer::AddHttpHeader(struct evhttp_request* ev_http_req, const std::string& req_id) {
    auto* ev_header = evhttp_request_get_output_headers(ev_http_req);
    if (ev_header == nullptr) {
        LOG_ERRO("{}", "ev_header is nullptr!");
        return false;
    }

    if (!AddCommonHeaders(ev_header, req_id)) {
        return false;
    }

    evhttp_add_header(ev_header, "Content-Type", "application/json");
    evhttp_add_header(ev_header, "Connection", "close");
    return true;
}

bool HttpServer::AddHttpOctetHeader(struct evhttp_request* ev_http_req, const std::string& req_id,
                                    const std::string& file_name, long fsize) {
    auto* ev_header = evhttp_request_get_output_headers(ev_http_req);
    if (ev_header == nullptr) {
        LOG_ERRO("{}", "ev_header is nullptr!");
        return false;
    }

    if (!AddCommonHeaders(ev_header, req_id)) {
        return false;
    }

    evhttp_add_header(ev_header, "Content-Type", "application/octet-stream");
    std::string content_length = std::to_string(fsize);
    evhttp_add_header(ev_header, "Content-Length", content_length.c_str());
    std::string attachmentFile = "attachment; filename=" + file_name;
    evhttp_add_header(ev_header, "Content-Disposition", attachmentFile.c_str());
    return true;
}

}  // namespace cosmo::network::http
