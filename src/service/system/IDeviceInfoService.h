/// @file IDeviceInfoService.h
/// @brief Aggregate device info service interface — inherits
///        IDeviceHardware and IHardwareQuery sub-interfaces.
///        Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "service/system/IDeviceHardware.h"
#include "service/system/IHardwareQuery.h"
#include "service/system/dto/SystemMsgTypes.h"
#include "util/MsgBaseTypes.h"

namespace cosmo::service {

/// Basic device information DTO for API responses.
struct DeviceBasicInfo {
    std::string devModel;         ///< Device model name.
    std::string devVersion;       ///< Firmware version.
    std::string softwareVersion;  ///< Software application version.
    std::string devSn;            ///< Device serial number.

    int64_t appRuntime{0};  ///< Application uptime in seconds.
};

/// Hardware resource utilization item for dashboard display.
struct HwResourceItem {
    std::string key;              ///< Resource key identifier.
    std::string name;             ///< Human-readable resource name.
    int usedPercent{0};           ///< Usage percentage (0–100).
    std::string usedSize;         ///< Used capacity string (e.g. "2.1 GiB").
    std::string unusedSize;       ///< Free capacity string.
    int available{0};             ///< Available units.
    std::string memoryDomain;     ///< Memory accounting scope; empty for non-memory resources.
    std::string totalSize{};      ///< Capacity of the reported scope, when known.
    std::string allocatedSize{};  ///< Configured allocation for this scope, when known.
};

/// Aggregate device info service providing device identity
/// and hardware resource monitoring.
class IDeviceInfoService : public IDeviceHardware, public IHardwareQuery {
public:
    virtual ~IDeviceInfoService() = default;

    // ── Device Info ──

    /// Get basic device information.
    /// @return Device info DTO.
    virtual DeviceBasicInfo GetDeviceInfo() = 0;

    /// Get hardware resource utilization summary.
    /// @param customScore [out] Computed health score (0.0–100.0).
    /// @return Vector of resource utilization items.
    virtual std::vector<HwResourceItem> GetHardwareResource(double& customScore) = 0;
};

}  // namespace cosmo::service
