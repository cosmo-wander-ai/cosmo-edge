/// @file HttpFileServerCliThread.h
/// @brief Worker thread for file upload/download operations.
#pragma once

#include <functional>

#include "network/http/HttpPost.h"
#include "network/msg/MsgThread.h"
#include "service/path/impl/file/HttpFileServerCli.h"
#include "service/path/impl/file/HttpFileServerCliCom.h"

namespace cosmo::service {

class CHttpFileServerCliThread : public cosmo::MsgThread {
public:
    explicit CHttpFileServerCliThread(const std::string& name,
                                      cosmo::network::http::HttpFileServerCli& fileServerCli,
                                      cosmo::network::http::HttpPost& httpPost,
                                      std::function<void(std::string)> onIpPortChangedCb = nullptr,
                                      size_t maxCount                                    = 0xffff);
    ~CHttpFileServerCliThread() override;

private:
    void HandleMsg(cosmo::MsgEnvelope& msg) override;

    /// Common retry-based upload logic shared by sync and async paths.
    /// @return 0 on success, -1 on failure.
    int RetryUpload(int max_retries, CUploadFileTask& task);

    void ProcessPointUploadFile(CPointUploadFileTask& task);
    void ProcessGetDownload(int max_retries, CGetDownloadTask& task);

    cosmo::network::http::HttpFileServerCli& file_server_cli_;
    cosmo::network::http::HttpPost& http_post_;
    std::function<void(std::string)> on_ip_port_changed_cb_;
};

}  // namespace cosmo::service
