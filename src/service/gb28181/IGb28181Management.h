#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace cosmo::service {
class IGb28181Management {
public:
    virtual ~IGb28181Management()                                 = default;
    virtual void Init()                                           = 0;
    virtual nlohmann::json Execute(const nlohmann::json& request) = 0;
    // Non-blocking demand notification from the existing channel status/preview path.
    virtual void Ensure(const std::string& channelId) = 0;
};
}  // namespace cosmo::service
