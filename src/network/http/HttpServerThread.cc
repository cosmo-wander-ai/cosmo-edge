// HttpServerThread — HTTP request handler thread

#include "network/http/HttpServerThread.h"

#include <event2/keyvalq_struct.h>
#include <event2/util.h>
#include <sys/queue.h>

#include <cstring>
#include <filesystem>
#include <memory>

#include "event2/http.h"
#include "network/http/HttpServer.h"
#include "network/http/MultipartStreamParser.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/Log.h"
#include "util/StringUtil.h"

namespace cosmo::network::http {

namespace chrono = std::chrono;

MsgHanderThread::MsgHanderThread(const std::string& name, HttpServer* server,
                                 std::unique_ptr<cosmo::IRequestDispatcher> dispatcher, size_t maxCount)
    : cosmo::MsgThread(name), handler_(std::move(dispatcher)), server_(server) {
    (void)maxCount;
}

MsgHanderThread::~MsgHanderThread() {
    Stop();
}

std::string MsgHanderThread::GetMtk(struct evkeyvalq* ev) {
    auto mtk = evhttp_find_header(ev, "mtk");
    if (!mtk) {
        LOG_WARN("{}", "MTK Empty");
        return "";
    }

    return std::string(mtk);
}

void MsgHanderThread::HandleMsg(cosmo::MsgEnvelope& msg) {
    is_thread_busy_.store(true, std::memory_order_relaxed);
    auto ptrAclTask = msg.ReleaseData();

    auto msgId = static_cast<InnerMsgId>(msg.GetMsgId());
    switch (msgId) {
        case InnerMsgId::kHttpReq:
            if (auto pTask = static_cast<HttpReqTask*>(ptrAclTask.get())) {
                ProcessHttpReqTask(*pTask);
            }
            break;
        case InnerMsgId::kHttpOctetReq:
            if (auto pTask = static_cast<HttpReqTask*>(ptrAclTask.get())) {
                ProcessHttpOctetReqTask(*pTask);
            }
            break;
        default:
            break;
    }

    is_thread_busy_.store(false, std::memory_order_relaxed);
}

void MsgHanderThread::DelTmpPath(const HttpReqTask& task) {
    if (task.has_tmp_path) {
        cosmo::util::RemovePath(task.tmp_file_path);
    }
}

std::map<std::string, std::string> MsgHanderThread::ParseMultipartFields(HttpReqTask& task,
                                                                         const char* boundaryContent) {
    std::map<std::string, std::string> form_fields;
    auto evR                   = task.request;
    struct evbuffer* input_buf = evhttp_request_get_input_buffer(evR);
    std::string boundary       = boundaryContent;
    MultipartStreamParser parser(boundary);
    task.tmp_file_path = server_->callbacks().get_upload_tmp_path();
    task.has_tmp_path  = true;
    auto res           = parser.ParseToFile(input_buf, [&](const std::string& filename) {
        return (std::filesystem::path(task.tmp_file_path) / filename).string();
    });
    if (!res.ok) {
        LOG_ERRO("ParseMultipartFields failed: {}", res.err);
        return form_fields;
    }
    form_fields = std::move(res.fields);

    return form_fields;
}

void MsgHanderThread::ProcessHttpReqTask(HttpReqTask& task) {
    auto evR = task.request;
    auto uri = evhttp_request_get_uri(evR);
    if (nullptr == uri) {
        LOG_WARN("{}", "Cant Get Url");
        return;
    }

    struct evkeyvalq* input_headers = evhttp_request_get_input_headers(evR);
    const char* xForwardedFor       = evhttp_find_header(input_headers, "x-forwarded-for");
    if (xForwardedFor) {
        LOG_INFO("{} Handle {}, From:{}", Name(), uri, xForwardedFor);
    } else {
        LOG_INFO("{} Handle {}", Name(), uri);
    }
    std::string request_id;

    std::string body;
    const char* content_type = evhttp_find_header(input_headers, "Content-Type");
    if (content_type && strstr(content_type, "multipart/form-data") != nullptr) {
        if (!ExtractMultipartBody(task, content_type, uri, body)) {
            return;
        }
    } else {
        struct evbuffer* evbuf = evhttp_request_get_input_buffer(evR);
        size_t body_len        = evbuffer_get_length(evbuf);

        if (body_len > 0) {
            body.resize(body_len, 0);
            memcpy(&body[0], evbuffer_pullup(evbuf, -1), body_len);
        }
    }

    // Truncate large bodies in logs to avoid memory/disk pressure
    if (body.size() > 4096) {
        LOG_INFO("{} Receive uri:{}, body: {:.4096} ...", Name(), uri, body);
    } else {
        LOG_INFO("{} Receive uri:{}, body: {}", Name(), uri, body);
    }

    std::string response;

    if (!handler_->SupportsRoute(uri)) {
        LOG_INFO("{} Handle uri:{} BAD REQUEST", Name(), uri);
        SendHttpAck(HttpResponseCode::kBadRequest, evR, std::string("{}"), std::move(request_id));
        DelTmpPath(task);
        return;
    }

    auto mtk = GetMtk(input_headers);
    if (!handler_->DispatchRequest(uri, mtk, body, response)) {
        LOG_INFO("{} Handle uri:{} With {} Ms Rsp: MV_HTTP_NEED_AUTHENTICATE", Name(), uri,
                 chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - task.request_time)
                     .count());
        SendHttpAck(HttpResponseCode::kNeedAuthenticate, evR, std::move(response), std::move(request_id));
        DelTmpPath(task);
        return;
    }

    LOG_INFO(
        "{} Handle uri:{} With {} Ms Rsp: {:.4096}{}", Name(), uri,
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - task.request_time).count(),
        response, response.size() > 4096 ? " ..." : "");
    SendHttpAck(HttpResponseCode::kOk, evR, std::move(response), std::move(request_id));
    DelTmpPath(task);
}

bool MsgHanderThread::ExtractMultipartBody(HttpReqTask& task, const char* contentType, const char* uri,
                                           std::string& body) {
    auto evR                        = task.request;
    struct evkeyvalq* input_headers = evhttp_request_get_input_headers(evR);
    LOG_INFO("{} Receive uri:{},Handle multipart/form-data", Name(), uri);
    const char* contentLength = evhttp_find_header(input_headers, "Content-Length");
    if (contentLength) {
        LOG_INFO("Content-Length:{}", contentLength);
    }
    const char* boundary_start = strstr(contentType, "boundary=");
    if (!boundary_start) {
        LOG_INFO("{} Receive uri:{},Handle boundary Failed", Name(), uri);
        return false;
    }
    boundary_start += 9;  // skip "boundary="
    char boundary[256];
    const char* boundary_end = strchr(boundary_start, ';');
    if (!boundary_end) {
        boundary_end = boundary_start + strlen(boundary_start);
    }

    size_t boundary_len = static_cast<size_t>(boundary_end - boundary_start);
    if (boundary_len >= sizeof(boundary)) {
        boundary_len = sizeof(boundary) - 1;
    }

    strncpy(boundary, boundary_start, boundary_len);
    boundary[boundary_len] = '\0';
    auto fields            = ParseMultipartFields(task, boundary);
    (void)cosmo::util::EncodeJson(fields, body);
    return true;
}

void MsgHanderThread::SendHttpAck(HttpResponseCode code, struct evhttp_request* req, std::string response,
                                  std::string request_id) {
    auto ackTask = std::make_unique<HttpAckTask>(static_cast<int>(code), req, std::move(response),
                                                 std::move(request_id));
    cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpAck),
                           std::unique_ptr<cosmo::MsgTask>(ackTask.release()));
    server_->Put(std::move(msg));
}

void MsgHanderThread::ProcessHttpOctetReqTask(HttpReqTask& task) {
    auto evR = task.request;
    std::string response;
    std::string request_id;
    auto uri           = evhttp_request_get_uri(evR);
    auto ackTask       = std::make_unique<HttpAckTask>(static_cast<int>(HttpResponseCode::kOk), evR,
                                                 std::move(response), std::move(request_id));
    ackTask->file_name = std::string(cosmo::util::GetLastPathSegment(uri));
    ackTask->file_path = server_->callbacks().get_user_data_path() + uri;
    LOG_INFO("{} Handle {} interface:{}", Name(), uri, ackTask->file_name);
    cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpOctetAck),
                           std::unique_ptr<cosmo::MsgTask>(ackTask.release()));
    server_->Put(std::move(msg));
}

}  // namespace cosmo::network::http
