#pragma once

#include <string>

namespace cosmo {

/// Abstract request dispatcher for HTTP/MQTT message routing.
/// Implemented by ApiRouter (api layer), consumed by HttpServer (network layer).
class IRequestDispatcher {
public:
    virtual ~IRequestDispatcher() = default;

    /// Check if the given URI is supported
    virtual bool SupportsRoute(const std::string& uri) = 0;

    /// Dispatch a message. Returns true if authenticated, false otherwise.
    /// @param uri       request URI
    /// @param mtk       authentication token
    /// @param body      request body (JSON)
    /// @param response  [out] response body
    virtual bool DispatchRequest(const std::string& uri, const std::string& mtk, const std::string& body,
                                 std::string& response) = 0;
};

}  // namespace cosmo
