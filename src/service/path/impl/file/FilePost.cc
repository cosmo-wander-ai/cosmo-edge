// FilePost — File Post implementation.

#include "service/path/impl/file/FilePost.h"

#include <ctime>
#include <fstream>

#include "util/Log.h"

namespace cosmo::network::http {

static constexpr const char* kTag = "FilePost";

FilePost::FilePost(const std::string& url) : http_post_(url, str_hnd_) {
    BoundaryGen();
}

void FilePost::SetPostUrl(const std::string& url) {
    http_post_.SetPostUrl(url);
}

bool FilePost::SetFile(const std::string& key, const std::string& filename, const std::string& filetype) {
    data_.append("--" + boundary_ + "\r\n");
    data_.append("Content-Disposition: form-data; name=\"" + key + "\"; filename=\"" + filename + "\"\r\n");
    data_.append("Content-Type: " + filetype + "\r\n\r\n");

    std::ifstream ifs(filename, std::ios::binary);
    if (ifs.is_open()) {
        char buf[1024]{};
        do {
            ifs.read(buf, sizeof(buf));
            data_.append(buf, ifs.gcount());
        } while (ifs);
    } else {
        LOG_INFO("{} cannot open file [{}].", kTag, filename);
        return false;
    }
    data_.append("\r\n");
    return true;
}

void FilePost::AppendData(const std::string& key, const std::string& value) {
    data_.append("--" + boundary_ + "\r\n");
    data_.append("Content-Disposition: form-data; name=\"" + key + "\"\r\n\r\n");
    data_.append(value + "\r\n");
}

long FilePost::Submit() {
    data_.append("--" + boundary_ + "--\r\n");
    http_post_.SetData(data_);
    http_post_.SetContentType("multipart/form-data; boundary=" + boundary_);
    return http_post_.Submit();
}

std::string FilePost::GetContentType() const {
    return http_post_.GetContentType();
}

std::string FilePost::GetContent() const {
    return str_hnd_.GetData();
}

void FilePost::BoundaryGen() {
    boundary_.reserve(41);
    boundary_.assign(27, '-');
    char buf[15]{};
    snprintf(buf, 15, "0000%10s", std::to_string(std::time(nullptr)).c_str());
    boundary_.append(buf);
}

void FilePost::SetProxy(const std::string& proxy, const std::string& username, const std::string& password) {
    http_post_.SetProxy(proxy, username, password);
}

HttpRequest& FilePost::GetHttpRequest() {
    return http_post_;
}

}  // namespace cosmo::network::http