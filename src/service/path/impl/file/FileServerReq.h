#pragma once

#include <string>
#include <vector>

#include "network/http/HttpRequest.h"

namespace cosmo::network::http {

class FileServerReq {
public:
    explicit FileServerReq(const std::string& url);

    FileServerReq(const FileServerReq&)            = delete;
    FileServerReq& operator=(const FileServerReq&) = delete;

    void SetPostUrl(const std::string& url);
    bool SetMsgBody(const std::string& body);
    void AppendData(const std::string& key, const std::string& value);

    // Submit request, returns HTTP status code
    long Submit();

    std::string GetContentType() const;
    std::string GetContent() const;

    // Set proxy
    void SetProxy(const std::string& proxy, const std::string& username = "",
                  const std::string& password = "");
    // Get underlying HTTP request object
    HttpRequest& GetHttpRequest();

private:
    void BoundaryGen();

    HttpStringHandler str_hnd_;
    HttpRequest http_post_;
    std::string data_;
    std::string boundary_;
};

}  // namespace cosmo::network::http
