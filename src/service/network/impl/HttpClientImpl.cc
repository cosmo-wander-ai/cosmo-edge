// HttpClientImpl — Concrete IHttpClient implementation that delegates to cosmo::network::http::H...

#include "service/network/impl/HttpClientImpl.h"

#include "network/http/HttpRequest.h"

namespace cosmo::service {

HttpResponse HttpClientImpl::Post(const std::string& url, const std::string& data,
                                  const std::string& contentType, long connectTimeoutSec, long timeoutSec,
                                  const std::vector<std::pair<std::string, std::string>>& headers) {
    cosmo::network::http::HttpStringHandler handler;
    cosmo::network::http::HttpRequest httpReq(url, handler);

    httpReq.AppendHeader("Expect", "");
    for (const auto& header : headers) {
        httpReq.AppendHeader(header.first, header.second);
    }
    httpReq.SetContentType(contentType);
    httpReq.SetData(data);
    httpReq.SetConnectTimeout(connectTimeoutSec);
    httpReq.SetTimeout(timeoutSec);

    HttpResponse response;
    response.statusCode = httpReq.Submit();
    response.body       = handler.GetData();
    return response;
}

}  // namespace cosmo::service
