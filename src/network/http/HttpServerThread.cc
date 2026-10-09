// HttpServerThread — HTTP request handler thread

#include "network/http/HttpServerThread.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

#include "network/http/HttpServer.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::network::http {

namespace chrono = std::chrono;

MsgHanderThread::MsgHanderThread(const std::string& name, HttpServer* server,
                                 std::unique_ptr<cosmo::IRequestDispatcher> dispatcher, size_t maxCount)
    : cosmo::MsgThread(name, maxCount), handler_(std::move(dispatcher)), server_(server) {}

MsgHanderThread::~MsgHanderThread() {
    Stop();
}

void MsgHanderThread::HandleMsg(cosmo::MsgEnvelope& msg) {
    is_thread_busy_.store(true, std::memory_order_relaxed);
    auto ptrAclTask = msg.ReleaseData();
    HttpReqTask* request_task{nullptr};

    try {
        auto msg_id  = static_cast<InnerMsgId>(msg.GetMsgId());
        request_task = static_cast<HttpReqTask*>(ptrAclTask.get());
        if (request_task != nullptr) {
            switch (msg_id) {
                case InnerMsgId::kHttpReq:
                    ProcessHttpReqTask(*request_task);
                    break;
                case InnerMsgId::kHttpOctetReq:
                    ProcessHttpOctetReqTask(*request_task);
                    break;
                default:
                    break;
            }
            DelTmpPath(*request_task);
        }
    } catch (...) {
        if (request_task != nullptr) {
            DelTmpPath(*request_task);
        }
        is_thread_busy_.store(false, std::memory_order_relaxed);
        throw;
    }

    is_thread_busy_.store(false, std::memory_order_relaxed);
}

void MsgHanderThread::ClearMsg(cosmo::MsgEnvelope& msg) {
    auto msg_id = static_cast<InnerMsgId>(msg.GetMsgId());
    if (msg_id == InnerMsgId::kHttpReq || msg_id == InnerMsgId::kHttpOctetReq) {
        if (auto* task = static_cast<HttpReqTask*>(msg.GetData())) {
            DelTmpPath(*task);
        }
    }
    cosmo::MsgThread::ClearMsg(msg);
}

void MsgHanderThread::DelTmpPath(const HttpReqTask& task) {
    if (task.has_tmp_path) {
        cosmo::util::RemovePath(task.tmp_file_path);
    }
}

void MsgHanderThread::ProcessHttpReqTask(HttpReqTask& task) {
    const auto& uri = task.interface;
    if (!task.x_forwarded_for.empty()) {
        LOG_DEBUG("{} Handle {}, From:{}", Name(), uri, task.x_forwarded_for);
    } else {
        LOG_DEBUG("{} Handle {}", Name(), uri);
    }
    std::string request_id;

    // Request bodies can contain passwords, tokens, stream credentials, and
    // model parameters. Log metadata only, at debug level, so routine polling
    // cannot disclose payloads or flood production logs.
    LOG_DEBUG("{} Receive uri:{} request_bytes:{}", Name(), uri, task.body.size());

    RequestDispatchResponse dispatch_response;

    if (!handler_->SupportsRoute(uri)) {
        LOG_WARN("{} Handle uri:{} BAD REQUEST", Name(), uri);
        SendHttpAck(HttpResponseCode::kBadRequest, task.request_token, std::string("{}"),
                    std::move(request_id));
        return;
    }

    if (task.mtk.empty()) {
        LOG_WARN("{}", "MTK Empty");
    }
    RequestDispatchContext context;
    context.uri                 = uri;
    context.credential          = task.mtk;
    context.principal           = task.principal;
    context.http_method         = task.http_method;
    context.upload_id           = task.upload_id;
    context.upload_offset       = task.upload_offset;
    context.multipart_file_path = task.multipart_file_path;
    context.multipart_file_name = task.multipart_file_name;
    context.multipart_file_size = task.multipart_file_size;
    context.transport           = RequestTransport::kHttp;
    if (!handler_->DispatchRequestResponse(context, task.body, dispatch_response)) {
        LOG_DEBUG("{} Handle uri:{} status:{} elapsed_ms:{} response_bytes:{}", Name(), uri,
                  static_cast<int>(HttpResponseCode::kNeedAuthenticate),
                  chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - task.request_time)
                      .count(),
                  dispatch_response.body.size());
        SendHttpAck(HttpResponseCode::kNeedAuthenticate, task.request_token,
                    std::move(dispatch_response.body), std::move(request_id));
        return;
    }

    if (!dispatch_response.file_path.empty()) {
        auto ack_task =
            std::make_unique<HttpAckTask>(static_cast<int>(HttpResponseCode::kOk), task.request_token,
                                          std::string{}, std::move(request_id));
        ack_task->file_path              = std::move(dispatch_response.file_path);
        ack_task->file_name              = std::move(dispatch_response.file_name);
        ack_task->range_request          = task.range_request;
        ack_task->delete_file_after_send = dispatch_response.delete_file_after_send;
        LOG_DEBUG("{} Handle {} file:{}", Name(), uri, ack_task->file_name);
        cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpOctetAck), std::move(ack_task));
        server_->Put(std::move(msg));
        return;
    }

    LOG_DEBUG(
        "{} Handle uri:{} status:{} elapsed_ms:{} response_bytes:{}", Name(), uri,
        static_cast<int>(HttpResponseCode::kOk),
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - task.request_time).count(),
        dispatch_response.body.size());
    SendHttpAck(HttpResponseCode::kOk, task.request_token, std::move(dispatch_response.body),
                std::move(request_id));
}

void MsgHanderThread::SendHttpAck(HttpResponseCode code, HttpRequestToken request_token, std::string response,
                                  std::string request_id) {
    auto ackTask = std::make_unique<HttpAckTask>(static_cast<int>(code), request_token, std::move(response),
                                                 std::move(request_id));
    cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpAck), std::move(ackTask));
    server_->Put(std::move(msg));
}

void MsgHanderThread::ProcessHttpOctetReqTask(HttpReqTask& task) {
    static constexpr std::string_view kLogPrefix{"/logs/"};
    std::string response;
    std::string request_id;
    const auto& uri = task.interface;

    RequestDispatchContext context;
    context.uri        = uri;
    context.credential = task.mtk;
    context.transport  = RequestTransport::kHttp;
    if (handler_->InspectRequest(context, false) != RequestAdmission::kAllowed) {
        SendHttpAck(HttpResponseCode::kNeedAuthenticate, task.request_token, std::string("{}"),
                    std::move(request_id));
        return;
    }

    if (uri.compare(0, kLogPrefix.size(), kLogPrefix) != 0) {
        SendHttpAck(HttpResponseCode::kNotFound, task.request_token, std::string("{}"),
                    std::move(request_id));
        return;
    }

    const std::string file_name = uri.substr(kLogPrefix.size());
    if (!cosmo::path::IsSafePathComponent(file_name) || file_name.find_first_of("?#") != std::string::npos ||
        !server_->callbacks().get_log_path) {
        LOG_WARN("{} Reject invalid log path", Name());
        SendHttpAck(HttpResponseCode::kNotFound, task.request_token, std::string("{}"),
                    std::move(request_id));
        return;
    }

    const std::string log_root = server_->callbacks().get_log_path();
    std::string resolved_path;
    if (!cosmo::path::ResolveExistingPathWithinRoot(
            log_root, (std::filesystem::path(log_root) / file_name).string(),
            cosmo::path::PathEntryType::kRegularFile, resolved_path)) {
        LOG_WARN("{} Reject unavailable log file {}", Name(), file_name);
        SendHttpAck(HttpResponseCode::kNotFound, task.request_token, std::string("{}"),
                    std::move(request_id));
        return;
    }

    auto ackTask = std::make_unique<HttpAckTask>(static_cast<int>(HttpResponseCode::kOk), task.request_token,
                                                 std::move(response), std::move(request_id));
    ackTask->file_name     = file_name;
    ackTask->file_path     = std::move(resolved_path);
    ackTask->range_request = task.range_request;
    LOG_DEBUG("{} Handle {} interface:{}", Name(), uri, ackTask->file_name);
    cosmo::MsgEnvelope msg(static_cast<int>(InnerMsgId::kHttpOctetAck), std::move(ackTask));
    server_->Put(std::move(msg));
}

}  // namespace cosmo::network::http
