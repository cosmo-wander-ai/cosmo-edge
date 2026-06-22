// HttpRequestHandler — Http Request Handler implementation.

#include "network/http/HttpRequestHandler.h"

#include "util/Log.h"

namespace cosmo::network::http {

size_t HttpStringHandler::AppendData(const char* data, size_t size) {
    data_.append(data, size);
    return size;
}

HttpFileHandler::HttpFileHandler(const std::string& filename) : file_(filename) {}

size_t HttpFileHandler::AppendData(const char* data, size_t size) {
    if (file_.is_open()) {
        file_.write(data, size);
        return size;
    }
    return 0;
}

void HttpFileHandler::Flush() {
    file_.flush();
}

size_t HttpImageHandler::AppendData(const char* data, size_t size) {
    data_.insert(data_.end(), data, data + size);
    return size;
}

const std::string& HttpStringHandler::GetData() const {
    return data_;
}

const std::vector<u_char>& HttpImageHandler::GetImageData() const {
    return data_;
}

}  // namespace cosmo::network::http
