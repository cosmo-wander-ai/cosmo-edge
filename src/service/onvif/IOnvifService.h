#pragma once

#include <atomic>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace cosmo::service {

struct OnvifSourceResult {
    std::string mediaUrl;
    std::string error;
    uint64_t revision{0};
};

// Owns device configuration and credentials. Public metadata never contains secrets.
// Resolve runs on the existing per-channel demux thread, not on the camera CRUD lock.
class IOnvifService {
public:
    virtual ~IOnvifService()                                                                       = default;
    virtual void Init()                                                                            = 0;
    virtual nlohmann::json Interfaces() const                                                      = 0;
    virtual nlohmann::json Discover(const std::string& interfaceAddress, int timeoutMs)            = 0;
    virtual nlohmann::json Probe(const nlohmann::json& request)                                    = 0;
    virtual std::string Save(const nlohmann::json& request, const std::string& source = "")        = 0;
    virtual nlohmann::json Describe(const std::string& source) const                               = 0;
    virtual void Remove(const std::string& source)                                                 = 0;
    virtual uint64_t Revision(const std::string& source) const                                     = 0;
    virtual OnvifSourceResult Resolve(const std::string& source, const std::atomic<bool>& running) = 0;
    virtual void Invalidate(const std::string& source)                                             = 0;
};
}  // namespace cosmo::service
