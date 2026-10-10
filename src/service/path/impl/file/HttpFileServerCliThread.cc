// HttpFileServerCliThread — Worker thread for file upload operations.

#include "service/path/impl/file/HttpFileServerCliThread.h"

#include <exception>
#include <memory>

#include "network/http/HttpPost.h"
#include "service/path/dto/ClientMsgFile.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"

namespace cosmo::service {

static constexpr const char* kTag = "HttpFileServerCliThread";

/// Default maximum attempt count for upload operations.
static constexpr int kMaxRetries = 3;

/// Error codes that indicate an expired or invalid authentication token.
static constexpr const char* kErrCodeBadRequest = "400";
static constexpr const char* kErrCodeTokenExpA  = "180000010";
static constexpr const char* kErrCodeTokenExpB  = "180000009";

namespace {
    void Notify(CUploadFileTask& task, bool success) noexcept {
        if (!task.callback) {
            return;
        }
        try {
            task.callback(task.task_id, success, task.req);
        } catch (const std::exception& e) {
            LOG_WARN("{} upload callback threw: {}", kTag, e.what());
        } catch (...) {
            LOG_WARN("{} upload callback threw an unknown exception", kTag);
        }
    }
}  // namespace

CHttpFileServerCliThread::CHttpFileServerCliThread(const std::string& name,
                                                   cosmo::network::http::HttpFileServerCli& fileServerCli,
                                                   cosmo::network::http::HttpPost& httpPost,
                                                   std::function<void(std::string)> onIpPortChangedCb,
                                                   size_t maxCount)
    : cosmo::MsgThread(name, maxCount),
      file_server_cli_(fileServerCli),
      http_post_(httpPost),
      on_ip_port_changed_cb_(std::move(onIpPortChangedCb)) {}

CHttpFileServerCliThread::~CHttpFileServerCliThread() {
    Stop();
}

void CHttpFileServerCliThread::HandleMsg(cosmo::MsgEnvelope& msg) {
    auto task_ptr = msg.ReleaseData();
    if (static_cast<FileServerMsgId>(msg.GetMsgId()) != FileServerMsgId::kUploadFile) {
        return;
    }
    auto* upload = dynamic_cast<CUploadFileTask*>(task_ptr.get());
    if (!upload) {
        LOG_ERRO("{} Invalid task type for kUploadFile", kTag);
        return;
    }
    try {
        RetryUpload(kMaxRetries, *upload);
    } catch (const std::exception& e) {
        LOG_ERRO("{} task failed with exception: {}", kTag, e.what());
        Notify(*upload, false);
    } catch (...) {
        LOG_ERRO("{} task failed with an unknown exception", kTag);
        Notify(*upload, false);
    }
}

void CHttpFileServerCliThread::ClearMsg(cosmo::MsgEnvelope& msg) {
    auto task_ptr = msg.ReleaseData();
    if (auto* upload = dynamic_cast<CUploadFileTask*>(task_ptr.get())) {
        Notify(*upload, false);
    }
}

int CHttpFileServerCliThread::RetryUpload(int max_retries, CUploadFileTask& task) {
    const std::string original_file_url = task.file_url;
    const bool absolute_file_url =
        original_file_url.compare(0, 7, "http://") == 0 || original_file_url.compare(0, 8, "https://") == 0;
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        LOG_INFO("{} Start to upload [{}], file size is [{}], attempt {}/{}", kTag, task.upload_filepath,
                 cosmo::util::GetFileSize(task.upload_filepath), attempt + 1, max_retries);

        task.file_url =
            absolute_file_url ? original_file_url : file_server_cli_.GetIpPort() + original_file_url;

        task.req_get_file_url = {};
        const bool submitted  = file_server_cli_.HttpclientSubmit(
            cosmo::network::http::FileServerClientType::kUploadFile, task.upload_filepath,
            task.req_get_file_url, task.bucket, task.file_url);

        if (submitted && 1 == task.req_get_file_url.resCode) {
            LOG_INFO("{} Upload File[{}] To FileServer[{}] Success", kTag, task.upload_filepath,
                     task.file_url);
            Notify(task, true);
            return 0;
        }

        // Token expired — refresh and retry
        cosmo::MsgResBase res_msg;
        if (!task.req_get_file_url.resMsg.empty()) {
            res_msg = task.req_get_file_url.resMsg.front();
        }
        if (res_msg.msgCode == kErrCodeBadRequest || res_msg.msgCode == kErrCodeTokenExpA ||
            res_msg.msgCode == kErrCodeTokenExpB) {
            cosmo::CMsgReqGetFileServerConfig req;
            std::string response;
            bool get_result = http_post_.GetFileServerConfig(response);
            if (get_result && !cosmo::util::DecodeJson(response, req)) {
                LOG_ERRO("HTTP POST response DecodeJson failed, response_bytes:{}", response.size());
                get_result = false;
            }
            if (get_result && req.resCode == cosmo::kClientRspSuccess) {
                LOG_INFO("{} Token refreshed, retrying upload", kTag);
                file_server_cli_.SetIpPort(req.resData.fileServerUrl);
                file_server_cli_.SetUserToken(req.resData.user, req.resData.token);
                if (on_ip_port_changed_cb_) {
                    on_ip_port_changed_cb_(req.resData.fileServerUrl);
                }
            } else {
                LOG_ERRO("{} getFileServerConfig failed", kTag);
            }
        }
    }

    // All retries exhausted
    LOG_ERRO("{} UploadFile[{}] failed after {} retries", kTag, task.upload_filepath, max_retries);
    Notify(task, false);
    return -1;
}

}  // namespace cosmo::service
