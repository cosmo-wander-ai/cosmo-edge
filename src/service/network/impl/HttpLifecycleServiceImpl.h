#pragma once

#include <functional>
#include <memory>

#include "service/network/IHttpLifecycle.h"
#include "util/IRequestDispatcher.h"

namespace cosmo::network::http {
class HttpServer;
}  // namespace cosmo::network::http

namespace cosmo::service {

class HttpLifecycleServiceImpl final : public IHttpLifecycle {
public:
    using DispatcherFactory = std::function<std::unique_ptr<cosmo::IRequestDispatcher>()>;

    /// @param dispatcher_factory  Factory that creates IRequestDispatcher instances
    ///                            for the HTTP thread pool.
    explicit HttpLifecycleServiceImpl(DispatcherFactory dispatcher_factory);
    ~HttpLifecycleServiceImpl() override;

    HttpLifecycleServiceImpl(const HttpLifecycleServiceImpl&)            = delete;
    HttpLifecycleServiceImpl& operator=(const HttpLifecycleServiceImpl&) = delete;

    // IHttpLifecycle
    void InitHttpServer(const std::string& host, uint16_t port) override;
    void RunHttpLoop() override;
    void StopHttpServer() override;

private:
    std::unique_ptr<cosmo::network::http::HttpServer> http_server_;
    DispatcherFactory dispatcher_factory_;
};

}  // namespace cosmo::service
