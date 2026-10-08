#pragma once

#include "mem/AllocatorCpu.h"
#include "mem/MemoryPoolMng.h"

namespace cosmo::test {

// Provide host storage for frame tests without starting device services.
// Declare before any frames so all buffers are released before the pool.
class ScopedFrameMemoryPool final {
public:
    ScopedFrameMemoryPool() {
        mem::SetMemoryPoolContext(&pool_);
    }
    ~ScopedFrameMemoryPool() {
        mem::SetMemoryPoolContext(nullptr);
    }
    ScopedFrameMemoryPool(const ScopedFrameMemoryPool&)            = delete;
    ScopedFrameMemoryPool& operator=(const ScopedFrameMemoryPool&) = delete;

private:
    mem::MemoryPoolMng pool_{std::make_unique<mem::AllocatorCpu>(), {4096}};
};

}  // namespace cosmo::test
