#pragma once

#include <chrono>
#include <map>
#include <mutex>

#include "service/onvif/IOnvifService.h"
#include "service/onvif/impl/OnvifProtocol.h"

namespace cosmo::service {
class OnvifServiceImpl final : public IOnvifService {
public:
    void Init() override;
    nlohmann::json Interfaces() const override;
    nlohmann::json Discover(const std::string& interfaceAddress, int timeoutMs) override;
    nlohmann::json Probe(const nlohmann::json& request) override;
    std::string Save(const nlohmann::json& request, const std::string& source = "") override;
    nlohmann::json Describe(const std::string& source) const override;
    void Remove(const std::string& source) override;
    uint64_t Revision(const std::string& source) const override;
    OnvifSourceResult Resolve(const std::string& source, const std::atomic<bool>& running) override;
    void Invalidate(const std::string& source) override;

private:
    struct Entry {
        onvif::Config config;
        uint64_t revision{1};
        std::string uri;
        std::string error;
        bool resolving{false};
        std::chrono::steady_clock::time_point retryAfter{};
    };
    onvif::Config ReadRequest(const nlohmann::json& request, const std::string& source) const;
    void Persist(const std::map<std::string, Entry>& entries) const;
    mutable std::mutex entries_mtx_;
    std::mutex writer_mtx_;
    std::map<std::string, Entry> entries_;
    std::string directory_;
    std::string key_;
    bool ready_{false};  // Set once during startup, before request threads exist.
    std::atomic<int> active_requests_{0};
};
}  // namespace cosmo::service
