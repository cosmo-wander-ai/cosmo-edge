// IMemoryPoolService implementation — owns MemoryPoolMng lifecycle.
// Registers itself with the legacy SetMemoryPoolContext() compatibility layer
// so existing code (VideoFrame.cc) can continue using GetMemoryPool().

#pragma once

#include <memory>

#include "service/infra/IMemoryPoolService.h"

namespace cosmo::mem {
class MemoryPoolMng;
}

namespace cosmo::service {

class MemoryPoolServiceImpl final : public IMemoryPoolService {
public:
    MemoryPoolServiceImpl();
    ~MemoryPoolServiceImpl() override;

    MemoryPoolServiceImpl(const MemoryPoolServiceImpl&)            = delete;
    MemoryPoolServiceImpl& operator=(const MemoryPoolServiceImpl&) = delete;

    cosmo::mem::Block* Acquire(size_t size) override;
    void Recycle(cosmo::mem::Block* block) override;
    std::vector<cosmo::mem::PoolStatus> Status() override;
    std::string OutputMallocBuf() override;

private:
    std::unique_ptr<cosmo::mem::MemoryPoolMng> pool_;
};

}  // namespace cosmo::service
