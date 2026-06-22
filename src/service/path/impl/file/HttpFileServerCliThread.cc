// HttpFileServerCliThread — Worker thread for file upload/download operations.

#include "service/path/impl/file/HttpFileServerCliThread.h"

#include <memory>

#include "network/http/HttpPost.h"
#include "service/path/dto/ClientMsgFile.h"
#include "util/FileUtil.h"
#include "util/Log.h"

namespace cosmo::service {

static constexpr const char* kTag = "HttpFileServerCliThread";

/// Default maximum retry count for upload / download operations.
static constexpr int kMaxRetries = 3;

/// Error codes that indicate an expired or invalid authentication token.
static constexpr const char* kErrCodeBadRequest = "400";
static constexpr const char* kErrCodeTokenExpA  = "180000010";
static constexpr const char* kErrCodeTokenExpB  = "180000009";

CHttpFileServerCliThread::CHttpFileServerCliThread(const std::string& name,
                                                   cosmo::network::http::HttpFileServerCli& fileServerCli,
                                                   cosmo::network::http::HttpPost& httpPost,
                                                   std::function<void(std::string)> onIpPortChangedCb,
                                                   size_t /*maxCount*/)
    : cosmo::MsgThread(name),
      file_server_cli_(fileServerCli),
      http_post_(httpPost),
      on_ip_port_changed_cb_(std::move(onIpPortChangedCb)) {}

CHttpFileServerCliThread::~CHttpFileServerCliThread() {
    Stop();
}

void CHttpFileServerCliThread::HandleMsg(cosmo::MsgEnvelope& msg) {
    auto task_ptr = msg.ReleaseData();

    switch (static_cast<FileServerMsgId>(msg.GetMsgId())) {
        case FileServerMsgId::kUploadFile:
            if (auto* upload = dynamic_cast<CUploadFileTask*>(task_ptr.get())) {
                RetryUpload(kMaxRetries, *upload);
            } else {
                LOG_ERRO("{} Invalid task type for kUploadFile", kTag);
            }
            break;
        case FileServerMsgId::kPointUploadFile:
            if (auto* upload = dynamic_cast<CPointUploadFileTask*>(task_ptr.get())) {
                ProcessPointUploadFile(*upload);
            } else {
                LOG_ERRO("{} Invalid task type for kPointUploadFile", kTag);
            }
            break;
        case FileServerMsgId::kGetDownload:
            if (auto* download = dynamic_cast<CGetDownloadTask*>(task_ptr.get())) {
                ProcessGetDownload(kMaxRetries, *download);
            } else {
                LOG_ERRO("{} Invalid task type for kGetDownload", kTag);
            }
            break;
        default:
            break;
    }
}

// Unified retry upload — replaces both uploadFileSync and processUploadFile.
int CHttpFileServerCliThread::RetryUpload(int max_retries, CUploadFileTask& task) {
    auto callback = task.callback;

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        LOG_INFO("{} Start to upload [{}], file size is [{}], attempt {}/{}", kTag, task.upload_filepath,
                 cosmo::util::GetFileSize(task.upload_filepath), attempt + 1, max_retries);

        std::string ip_port = file_server_cli_.GetIpPort();
        if (task.file_url.substr(0, 4) != "http") {
            task.file_url = ip_port + task.file_url;
        }

        file_server_cli_.HttpclientSubmit(cosmo::network::http::FileServerClientType::kUploadFile,
                                          task.upload_filepath, task.req_get_file_url, task.bucket,
                                          task.file_url);

        if (1 == task.req_get_file_url.resCode) {
            callback(task.task_id, true, task.req);
            LOG_INFO("{} Upload File[{}] To FileServer[{}] Success", kTag, task.upload_filepath,
                     task.file_url);
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
            auto get_result =
                http_post_
                    .HttpClientSubmit<cosmo::CMsgReqGetFileServerConfig, cosmo::CMsgReqGetFileServerConfig>(
                        cosmo::network::http::CliReqType::kCliGetFileSrv, req, req);
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
    callback(task.task_id, false, task.req);
    return -1;
}

void CHttpFileServerCliThread::ProcessPointUploadFile(CPointUploadFileTask& task) {
    auto callback = task.callback;
    auto upload_result =
        file_server_cli_.HttpclientSubmit(cosmo::network::http::FileServerClientType::kPointUploadFile,
                                          task.upload_filepath, task.req_point_upload);
    if (upload_result) {
        if (1 != task.req_point_upload.resCode) {
            LOG_ERRO("{} PointUploadFile failed, errcode[{}]", kTag, task.req_point_upload.resCode);
            callback(task.task_id, false, task.req);
            return;
        }
        LOG_INFO("{} Point Upload File To FileServer Success", kTag);
        callback(task.task_id, true, task.req);
    }
}

void CHttpFileServerCliThread::ProcessGetDownload(int max_retries, CGetDownloadTask& task) {
    auto callback = task.callback;

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        auto upload_result = file_server_cli_.HttpclientSubmit(
            cosmo::network::http::FileServerClientType::kGetDownload, task.resp_up_file,
            task.download_file_url, task.download_filepath);
        if (upload_result) {
            LOG_INFO("{} DownLoad File Success", kTag);
            callback(task.task_id, true, task.req);
            return;
        }
    }

    LOG_ERRO("{} DownLoad File Failed after {} retries", kTag, max_retries);
    callback(task.task_id, false, task.req);
}

}  // namespace cosmo::service
