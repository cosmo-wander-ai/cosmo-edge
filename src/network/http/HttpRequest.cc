// HttpRequest — Http Request implementation.

#include "network/http/HttpRequest.h"

#include <memory>

#include "curl/curl.h"
#include "util/Log.h"

namespace cosmo::network::http {

const char* GetMethodString(HttpRequestMethod method) {
    switch (method) {
        case HttpRequestMethod::kGet:
            return "GET";
        case HttpRequestMethod::kPost:
            return "POST";
        case HttpRequestMethod::kPut:
            return "PUT";
        case HttpRequestMethod::kDelete:
            return "DELETE";
        case HttpRequestMethod::kConnect:
            return "CONNECT";
        case HttpRequestMethod::kOptions:
            return "OPTIONS";
        case HttpRequestMethod::kTrace:
            return "TRACE";
        case HttpRequestMethod::kPatch:
            return "PATCH";
        default:
            return nullptr;
    }
}

HttpRequest::HttpRequest(const std::string& url, HttpRequestHandler& resultHandler)
    : HttpRequest(url, &resultHandler) {}

HttpRequest::HttpRequest(const std::string& url, HttpRequestHandler* result_handler)
    : url_(url),
      post_content_type_("text/plain"),
      result_handler_(result_handler),
      status_(0),
      timeout_(10L),
      connect_timeout_(10L),
      follow_location_(1L) {}

void HttpRequest::SetPostUrl(const std::string& url) {
    url_ = url;
}

void HttpRequest::AppendHeader(const std::string& key, const std::string& value) {
    header_.emplace_back(key + ": " + value);
}

void HttpRequest::SetData(const std::string& data) {
    data_ = data;
}

void HttpRequest::SetDataEx(std::string&& data) {
    data_ = std::move(data);
}

void HttpRequest::AppendData(const std::string& key, const std::string& value) {
    std::string sep;
    if (!data_.empty()) {
        sep = "&";
    }
    data_ += sep;
    data_ += key;
    data_ += "=";
    data_ += value;
}

const std::string& HttpRequest::GetContentType() const {
    return result_content_type_;
}

namespace {
    size_t PostCallback(char* ptr, size_t size, size_t nmemb, void* data) {
        auto len = size * nmemb;
        if (!data) {
            LOG_INFO("{}", "curl callback data is NULL");
            return len;
        }

        HttpRequestHandler* hnd = static_cast<HttpRequestHandler*>(data);
        return hnd->AppendData(ptr, len);
    }

}  // namespace

void HttpRequest::SetContentType(const std::string& contentType) {
    post_content_type_ = contentType;
}

long HttpRequest::Submit(HttpRequestMethod method) {
    CURL* curl = curl_easy_init();
    [[maybe_unused]] std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_easy_init_ptr(
        curl, curl_easy_cleanup);
    if (!curl) {
        LOG_ERRO("{}", "curl easy init fail");
        return -1;
    }

    CURLcode res = curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_URL fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PostCallback);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_WRITEFUNCTION fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, result_handler_);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_WRITEDATA fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    auto method_str = GetMethodString(method);
    if (method_str != nullptr) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_str);
    }

    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_location_);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    // SSL verification disabled for internal network environment
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);

    if (!interface_name_.empty()) {
        curl_easy_setopt(curl, CURLOPT_INTERFACE, interface_name_.c_str());
    }

    if (!proxy_.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy_.c_str());
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
        curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
        if (!proxy_username_.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, (proxy_username_ + ":" + proxy_password_).c_str());
        }
    }

    struct curl_slist* headers = nullptr;
    if (!data_.empty()) {
        std::string type_header = "Content-Type: " + post_content_type_;
        headers                 = curl_slist_append(headers, type_header.c_str());
        std::string size_header = "Content-Length: " + std::to_string(data_.size());
        headers                 = curl_slist_append(headers, size_header.c_str());
    }

    for (auto& header : header_) {
        headers = curl_slist_append(headers, header.c_str());
    }

    [[maybe_unused]] std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> curl_header_free(
        headers, curl_slist_free_all);
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (res != CURLE_OK) {
        LOG_ERRO("CURLOPT_HTTPHEADER fail : [{}]", curl_easy_strerror(res));
        return -1;
    }
    if (!data_.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data_.size());
    }

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_ERRO("curl perform fail : [{}]", curl_easy_strerror(res));
        return -1;
    }

    if (result_handler_) {
        result_handler_->Flush();
    }

    char* ct = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
    if (ct) {
        result_content_type_.assign(ct);
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_);
    if (method_str)
        LOG_INFO("HTTP Submit [{}][{}]", method_str, status_);
    else
        LOG_INFO("HTTP Submit [{}]", status_);
    return status_;
}

void HttpRequest::SetTimeout(long seconds) {
    timeout_ = seconds;
}

void HttpRequest::SetConnectTimeout(long seconds) {
    connect_timeout_ = seconds;
}

void HttpRequest::SetProxy(const std::string& proxy, const std::string& username,
                           const std::string& password) {
    proxy_          = proxy;
    proxy_username_ = username;
    proxy_password_ = password;
}

void HttpRequest::SetInterface(const std::string& interfaceName) {
    interface_name_ = interfaceName;
}

}  // namespace cosmo::network::http
