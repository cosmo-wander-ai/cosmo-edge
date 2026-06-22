// HTTP client interface — abstracts HTTP request capability
// so that device modules don't depend on network/cosmo::network::http::HttpRequest.

#pragma once
#include <string>
#include <utility>
#include <vector>

#include "service/detail/ServiceRegistry.h"

namespace cosmo::service {

struct HttpResponse {
    long statusCode{0};
    std::string body;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    /// Send HTTP POST request (JSON body), return status code and response body
    virtual HttpResponse Post(const std::string& url, const std::string& data,
                              const std::string& contentType = "application/json", long connectTimeoutSec = 2,
                              long timeoutSec = 3,
                              const std::vector<std::pair<std::string, std::string>>& headers = {}) = 0;
};

}  // namespace cosmo::service
