/// @file IHardwareQuery.h
/// @brief Hardware resource query interface.
///        ISP split from IAppInfoService.
///        Consumed by API layer for system overview hardware utilization display.
#pragma once

#include <cstddef>
#include <cstdint>

#include "service/system/dto/SystemMsgTypes.h"

namespace cosmo::service {

/// Read-only access to hardware resource utilization metrics:
/// CPU, GPU (NPU), memory, disk, and network I/O.
class IHardwareQuery {
public:
    virtual ~IHardwareQuery() = default;

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
