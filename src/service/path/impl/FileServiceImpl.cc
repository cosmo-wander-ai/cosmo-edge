// FileServiceImpl — Concrete IFileService implementation. Manages the file-upload

#include "service/path/impl/FileServiceImpl.h"

#include <fmt/format.h>

#include "network/http/HttpRequest.h"
#include "network/http/HttpRequestHandler.h"
#include "service/path/dto/FileServerMsgTypes.h"
#include "util/DateTimeFormat.h"
#include "util/Log.h"
#include "util/PeriodicTimer.h"
#include "util/UuidUtil.h"

namespace cosmo::service {

static constexpr const char* kTag = "FileServiceImpl";

constexpr int kCheckUrlTimeMsec = 3000;
constexpr int kMaxUrlSize       = 512;

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------
FileServiceImpl::~FileServiceImpl() {
    Shutdown();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void FileServiceImpl::Shutdown() {
    if (url_timer_) {
        url_timer_->Destroy();
        url_timer_.reset();
    }
    if (!worker_threads_.empty()) {
        for (auto& thread : worker_threads_) {
            thread->Stop();
        }
        worker_threads_.clear();
        has_file_server_ = false;
    }
}

// ---------------------------------------------------------------------------
// URL management internals
// ---------------------------------------------------------------------------
void FileServiceImpl::UpdateIpPort(const std::string& ip_port) {
    if (ip_port.empty()) {
        return;
    }

    // Build the updated map outside the lock to minimise hold time.
    std::unique_lock<std::shared_mutex> lck(url_mutex_);
    if (ip_port_ == ip_port) {
        return;
    }

    for (auto& [type, urls] : url_buffer_) {
        for (auto& url : urls) {
            url.replace(0, ip_port_.length(), ip_port);
        }
    }

    ip_port_ = ip_port;

    for (auto& [type, urls] : url_buffer_) {
        for (auto& item : urls) {
            const std::string kHttpPrefix = "http";
            if (item.substr(0, kHttpPrefix.length()) != kHttpPrefix) {
                item = ip_port_ + item;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Internal dispatch
// ---------------------------------------------------------------------------
int FileServiceImpl::DispatchUpload(std::string req_id, const cosmo::service::HFSCallBacK& cb_func, void* req,
                                    cosmo::FMsgRspGetFileUrl& req_crt, std::string filepath,
                                    std::string bucket, std::string file_url) {
    if (worker_threads_.empty()) {
        return -1;
    }

    cosmo::MsgEnvelope msg(static_cast<int>(cosmo::service::FileServerMsgId::kUploadFile),
                           std::make_unique<cosmo::service::CUploadFileTask>(
                               std::move(req_id), std::move(cb_func), req, std::move(bucket),
                               std::move(file_url), std::move(req_crt), std::move(filepath)));

    int idx = current_img_idx_.fetch_add(1, std::memory_order_relaxed) % kWorkerThreadCount;
    if (idx >= (kWorkerThreadCount / 2)) {
        idx = idx - (kWorkerThreadCount / 2);
    }

    return worker_threads_[idx]->Put(std::move(msg));
}

// ---------------------------------------------------------------------------
// IFileService overrides
// ---------------------------------------------------------------------------
bool FileServiceImpl::ReachLimit(FileType file_type) const {
    auto it = url_list_size_.find(file_type);
    if (it == url_list_size_.end()) {
        return false;
    }
    return file_type == FileType::Image ? it->second < kMaxUrlSize : it->second < kMaxUrlSize / 3;
}

std::string FileServiceImpl::GetFileUrl(FileType type) {
    std::unique_lock<std::shared_mutex> lck(url_mutex_);
    if (url_buffer_[type].empty()) {
        LOG_ERRO("{} url is empty!", kTag);
        return "";
    }

    auto date_time = cosmo::util::GetCurrentDateTime();
    auto year      = date_time.Date().Year();
    auto month     = date_time.Date().Month();
    auto date_str  = fmt::format("{}{:02d}", year, month);

    std::string url = url_buffer_[type].front();
    size_t pos      = url.rfind("/");
    if (pos != std::string::npos) {
        std::string suffix = url.substr(pos + 1);
        suffix.replace(0, date_str.length(), date_str);
        url.replace(pos + 1, suffix.length(), suffix);
    }

    url_buffer_[type].pop_front();
    url_list_size_[type] = static_cast<int>(url_buffer_[type].size());
    if (ReachLimit(type)) {
        LOG_WARN("{} url used too fast, maybe need get new url!", kTag);
    }
    return url;
}

void FileServiceImpl::UploadFile(const std::string& taskId, const FileUploadCallback& callback,
                                 void* userData, const std::string& ext, const std::string& localPath,
                                 const std::string& bucket, const std::string& remoteUrl) {
    cosmo::FMsgRspGetFileUrl req_crt{true, ext};
    DispatchUpload(
        taskId,
        [callback](const std::string& id, bool finished, void* ptr) {
            if (callback) {
                callback(id, finished, ptr);
            }
        },
        userData, req_crt, localPath, bucket, remoteUrl);
}

bool FileServiceImpl::DownloadFile(const std::string& url, std::vector<uint8_t>& data) {
    cosmo::network::http::HttpImageHandler http_hnd;
    cosmo::network::http::HttpRequest http_req(url, &http_hnd);
    http_req.SetTimeout(200);
    auto ret_code = static_cast<int>(http_req.Submit(cosmo::network::http::HttpRequestMethod::kGet));
    if (ret_code != 200) {
        LOG_ERRO("{} Download[{}] Fail! curl return [{}]", kTag, url, ret_code);
        return false;
    }

    data = http_hnd.GetImageData();
    return true;
}

}  // namespace cosmo::service
