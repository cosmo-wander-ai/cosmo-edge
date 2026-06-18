/// @file IMemoryDiag.h
/// @brief Memory diagnostic interface — VPU memory pool status and
///        malloc buffer inspection.
///        ISP split from IAppInfoService.
///        Consumed by API layer for memory pool status and malloc buffer inspection.
#pragma once

#include <string>
#include <vector>

#include "service/media/dto/MemoryPoolDto.h"

namespace cosmo::service {

/// Provides diagnostic information about VPU device memory pools
/// and system malloc allocations.
class IMemoryDiag {
public:
    virtual ~IMemoryDiag() = default;

    /// Get a human-readable summary of malloc buffer allocations.
    /// @return Multi-line string describing current allocations.
    virtual std::string OutputMallocBuf() = 0;

    /// Get the status of all VPU device memory pools.
    /// @return Vector of pool status DTOs with capacity and usage info.
    virtual std::vector<PoolStatusDto> GetMemoryPoolStatus() = 0;
};

}  // namespace cosmo::service
