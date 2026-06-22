// FileServerReq — File Server Req implementation.

#include "service/path/impl/file/FileServerReq.h"

#include <ctime>

#include "util/Log.h"

namespace cosmo::network::http {

FileServerReq::FileServerReq(const std::string& url) : http_post_(url, str_hnd_) {}

void FileServerReq::SetPostUrl(const std::string& url) {
    http_post_.SetPostUrl(url);
}

bool FileServerReq::SetMsgBody(const std::string& body) {
    data_ = body;
    return true;
}

void FileServerReq::AppendData(const std::string& key, const std::string& value) {
    data_.append("--" + boundary_ + "\r\n");
    data_.append("Content-Disposition: form-data; name=\"" + key + "\"\r\n\r\n");
    data_.append(value + "\r\n");
}

long FileServerReq::Submit() {
    http_post_.SetData(data_);
    http_post_.SetContentType("application/json;charset=UTF-8");
    return http_post_.Submit(HttpRequestMethod::kPost);
}

std::string FileServerReq::GetContentType() const {
    return http_post_.GetContentType();
}

std::string FileServerReq::GetContent() const {
    return str_hnd_.GetData();
}

void FileServerReq::SetProxy(const std::string& proxy, const std::string& username,
                             const std::string& password) {
    http_post_.SetProxy(proxy, username, password);
}

HttpRequest& FileServerReq::GetHttpRequest() {
    return http_post_;
}

}  // namespace cosmo::network::http