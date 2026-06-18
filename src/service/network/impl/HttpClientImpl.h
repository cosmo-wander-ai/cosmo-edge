// Concrete IHttpClient implementation that delegates to cosmo::network::http::HttpRequest.

#pragma once

#include "service/network/IHttpClient.h"

namespace cosmo::service {

class HttpClientImpl final : public IHttpClient {
public:
    HttpResponse Post(const std::string& url, const std::string& data,
                      const std::string& contentType = "application/json", long connectTimeoutSec = 2,
                      long timeoutSec = 3,
                      const std::vector<std::pair<std::string, std::string>>& headers = {}) override;
};

}  // namespace cosmo::service
