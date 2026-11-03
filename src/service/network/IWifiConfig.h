// Wi-Fi management uses a separate contract from the legacy WAN/LAN view.
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace cosmo::service {
class IWifiConfig {
public:
    virtual ~IWifiConfig()                                                 = default;
    virtual void Init()                                                    = 0;
    virtual void Stop()                                                    = 0;
    virtual nlohmann::json QueryStatus()                                   = 0;
    virtual nlohmann::json Scan()                                          = 0;
    virtual nlohmann::json Connect(const nlohmann::json& config)           = 0;
    virtual nlohmann::json QueryOperation(const std::string& operation_id) = 0;
    virtual nlohmann::json Confirm(const std::string& operation_id)        = 0;
    virtual nlohmann::json Disconnect()                                    = 0;
    virtual nlohmann::json Forget()                                        = 0;
    virtual bool Reset()                                                   = 0;
};
}  // namespace cosmo::service
