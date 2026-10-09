#pragma once

#include <cstdint>
#include <string>

namespace cosmo {

enum class RequestTransport {
    kHttp,
    kMqtt,
};

enum class RequestAdmission {
    kAllowed,
    kRouteNotFound,
    kUnauthorized,
};

struct RequestDispatchContext {
    std::string uri;
    std::string credential;
    std::string principal;
    std::string http_method;
    std::string upload_id;
    std::string upload_offset;
    // Server-generated provenance for the single file parsed from this HTTP
    // multipart request. These fields are never populated from JSON/form
    // fields and remain empty for non-multipart and MQTT requests.
    std::string multipart_file_path;
    std::string multipart_file_name;
    std::uint64_t multipart_file_size{0};
    RequestTransport transport{RequestTransport::kHttp};
};

struct RequestDispatchResponse {
    std::string body;
    std::string file_path;
    std::string file_name;
    bool delete_file_after_send{false};
};

/// Abstract request dispatcher for HTTP/MQTT message routing.
/// Implemented by ApiRouter (api layer), consumed by HttpServer (network layer).
class IRequestDispatcher {
public:
    virtual ~IRequestDispatcher() = default;

    /// Check if the given URI is supported
    virtual bool SupportsRoute(const std::string& uri) = 0;

    /// Perform route and credential admission without invoking a business handler.
    /// When require_known_route is false, only a valid HTTP session is accepted.
    /// On success, implementations must replace context.principal with the
    /// authenticated server-side identity (or leave it empty for anonymous or
    /// non-HTTP transports). Callers must never trust a pre-populated value.
    virtual RequestAdmission InspectRequest(RequestDispatchContext& context, bool require_known_route) = 0;

    /// Dispatch a message. Returns true if authenticated, false otherwise.
    /// @param context   request URI, transport, and transport credential
    /// @param body      request body (JSON)
    /// @param response  [out] response body
    virtual bool DispatchRequest(const RequestDispatchContext& context, const std::string& body,
                                 std::string& response) = 0;

    /// Dispatch a request that may produce a file response. Implementations
    /// that only return JSON can rely on this compatibility implementation.
    virtual bool DispatchRequestResponse(const RequestDispatchContext& context, const std::string& body,
                                         RequestDispatchResponse& response) {
        return DispatchRequest(context, body, response.body);
    }
};

}  // namespace cosmo
