/// @file IDeviceInfoService.h
/// @brief Aggregate device info service interface — inherits
///        IDeviceHardware sub-interface.
///        Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "service/system/IDeviceHardware.h"
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
    std::string key;         ///< Resource key identifier.
    std::string name;        ///< Human-readable resource name.
    int usedPercent{0};      ///< Usage percentage (0–100).
    std::string usedSize;    ///< Used capacity string (e.g. "2.1 GB").
    std::string unusedSize;  ///< Free capacity string.
    int available{0};        ///< Available units.
};

/// Aggregate device info service providing device identity
/// and hardware resource monitoring.
class IDeviceInfoService : public IDeviceHardware {
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

    // ── Hardware Resource Monitoring ──

    /// Get current CPU utilization percentage.
    virtual double GetCpuUtilization() = 0;

    /// Get current GPU (NPU) utilization info.
    virtual cosmo::MsgGpuInfo GetGpuUtilization() = 0;

    /// Get current memory utilization info.
    virtual cosmo::MsgMemoryInfo GetMemoryUtilization() = 0;

    /// Get current disk utilization info.
    virtual cosmo::MsgDiskInfo GetDiskUtilization() = 0;

    /// Get current network I/O utilization info.
    virtual cosmo::MsgNetInfo GetNetUtilization() = 0;

    /// Get available GPU (NPU) memory in megabytes.
    virtual int64_t GetAvailableGpuMemoryMB() = 0;

    /// Get the number of GPU (NPU) devices.
    virtual size_t GetGpuNum() = 0;
};

}  // namespace cosmo::service
