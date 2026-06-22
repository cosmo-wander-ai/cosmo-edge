// HTTP server lifecycle interface (ISP split from INetworkService).
// Consumed by app_init.cc for HTTP server bind, dispatch loop, and shutdown.
#pragma once

#include <cstdint>
#include <string>

namespace cosmo::service {

class IHttpLifecycle {
public:
    virtual ~IHttpLifecycle() = default;

    /// Initialize and bind the HTTP server on the given host:port.
    virtual void InitHttpServer(const std::string& host, uint16_t port) = 0;

    /// Blocking HTTP event dispatch loop. Call from main thread.
    virtual void RunHttpLoop() = 0;

    /// Shutdown the HTTP server (break the event loop).
    virtual void StopHttpServer() = 0;
};

}  // namespace cosmo::service
