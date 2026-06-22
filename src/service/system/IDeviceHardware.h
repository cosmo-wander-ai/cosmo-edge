/// @file IDeviceHardware.h
/// @brief Device hardware identity interface — serial number, model,
///        version, MAC addresses, and IP address queries.
///        ISP split from IDeviceInfoService.
///        Consumed by network/mqtt (GetDevModel, GetDevSn), device discovery, app_init.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cosmo::service {

/// Read-only access to device hardware identity information.
///
/// Provides serial number, model name, firmware version, specification,
/// MAC addresses, device ID, and IPv4 address.
class IDeviceHardware {
public:
    virtual ~IDeviceHardware() = default;

    /// Get the device serial number.
    virtual std::string GetDevSn() = 0;

    /// Get the device model name (e.g. "AIBox-BM1688").
    virtual std::string GetDevModel() = 0;

    /// Get the device firmware version string.
    virtual std::string GetDevVersion() = 0;

    /// Get the device hardware specification string.
    virtual std::string GetDevSpec() = 0;

    /// Get all MAC addresses as (interface name, MAC address) pairs.
    /// @return Vector of (interface name, MAC) pairs.
    virtual std::vector<std::pair<std::string, std::string>> GetMacs() = 0;

    /// Get the device unique identifier.
    virtual std::string GetDevId() = 0;

    /// Get the primary IPv4 address.
    virtual std::string GetIPV4() = 0;
};

}  // namespace cosmo::service
