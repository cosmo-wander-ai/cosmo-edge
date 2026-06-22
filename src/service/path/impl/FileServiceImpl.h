// Concrete IFileService implementation. Manages the file-upload
// thread pool and delegates to network/file utilities
// (HttpFileServerCliThread, HttpFileServerCli, HttpFileDownload).

#pragma once

#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "network/http/HttpPost.h"
#include "service/path/IFileService.h"
#include "service/path/impl/file/HttpFileServerCliCom.h"
#include "service/path/impl/file/HttpFileServerCliThread.h"
#include "util/PeriodicTimer.h"

namespace cosmo::service {

/// Number of worker threads for file upload operations.
static constexpr int kWorkerThreadCount = 4;

class FileServiceImpl final : public IFileService {
public:
    FileServiceImpl() = default;
    ~FileServiceImpl();

    // -- IFileService --
    std::string GetFileUrl(FileType type) override;

    void UploadFile(const std::string& taskId, const FileUploadCallback& callback, void* userData,
                    const std::string& ext, const std::string& localPath, const std::string& bucket,
                    const std::string& remoteUrl) override;

    bool DownloadFile(const std::string& url, std::vector<uint8_t>& data) override;

private:
    // --- Lifecycle ---
    void Shutdown();

    // --- Internal dispatch ---
    int DispatchUpload(std::string req_id, const cosmo::service::HFSCallBacK& cb_func, void* req,
                       cosmo::FMsgRspGetFileUrl& req_crt, std::string filepath, std::string bucket,
                       std::string file_url);

    // --- URL fetch and cache ---
    bool ReachLimit(FileType file_type) const;
    void UpdateIpPort(const std::string& ip_port);

    // --- Thread pool ---
    std::vector<std::unique_ptr<cosmo::service::CHttpFileServerCliThread>> worker_threads_;
    std::atomic<int> current_img_idx_{-1};
    cosmo::network::http::HttpFileServerCli http_file_cli_;
    cosmo::network::http::HttpPost http_post_;
    bool has_file_server_ = true;

    // --- URL cache ---
    std::map<FileType, std::list<std::string>> url_buffer_;
    std::map<FileType, int> url_list_size_{};
    std::shared_mutex url_mutex_;
    std::string ip_port_;
    std::unique_ptr<cosmo::PeriodicTimer> url_timer_;
};

}  // namespace cosmo::service
