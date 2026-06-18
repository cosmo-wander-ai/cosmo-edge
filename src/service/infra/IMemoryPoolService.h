// Memory pool service interface — abstracts device memory pool allocation.
// Provides Acquire/Recycle lifecycle for VPU device memory blocks.

#pragma once

#include <string>
#include <vector>

#include "mem/MemoryPoolMng.h"

namespace cosmo::service {

class IMemoryPoolService {
public:
    virtual ~IMemoryPoolService() = default;

    /// Acquire a device memory block of at least `size` bytes.
    virtual cosmo::mem::Block* Acquire(size_t size) = 0;

    /// Return a previously acquired block to the pool.
    virtual void Recycle(cosmo::mem::Block* block) = 0;

    /// Query pool utilization status for diagnostics.
    virtual std::vector<cosmo::mem::PoolStatus> Status() = 0;

    /// Human-readable pool allocation summary string.
    virtual std::string OutputMallocBuf() = 0;
};

}  // namespace cosmo::service
