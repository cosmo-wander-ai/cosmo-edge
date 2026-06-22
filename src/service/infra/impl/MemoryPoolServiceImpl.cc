// MemoryPoolServiceImpl — IMemoryPoolService implementation — owns MemoryPoolMng lifecycle.

#include "service/infra/impl/MemoryPoolServiceImpl.h"

#include "mem/IDeviceContext.h"
#include "mem/MemoryPoolMng.h"
#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "mem/AllocatorSophon.h"
#else
#include "mem/AllocatorCpu.h"
#endif
#include "service/detail/ServiceRegistry.h"
#include "util/Log.h"

namespace cosmo::service {

MemoryPoolServiceImpl::MemoryPoolServiceImpl() {
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    auto allocator = std::make_unique<cosmo::mem::AllocatorSophon>(
        ServiceRegistry::Instance().Get<cosmo::mem::IDeviceContext>());
#else
    auto allocator = std::make_unique<cosmo::mem::AllocatorCpu>();
#endif
    pool_ = std::make_unique<cosmo::mem::MemoryPoolMng>(std::move(allocator));
    cosmo::mem::SetMemoryPoolContext(pool_.get());
    LOG_INFO("{}", "MemoryPoolServiceImpl: pool initialized and context registered");
}

MemoryPoolServiceImpl::~MemoryPoolServiceImpl() {
    LOG_INFO("{}", "MemoryPoolServiceImpl: destroying pool");
    cosmo::mem::SetMemoryPoolContext(nullptr);
    pool_.reset();
}

cosmo::mem::Block* MemoryPoolServiceImpl::Acquire(size_t size) {
    return pool_->Acquire(size);
}

void MemoryPoolServiceImpl::Recycle(cosmo::mem::Block* block) {
    pool_->Recycle(block);
}

std::vector<cosmo::mem::PoolStatus> MemoryPoolServiceImpl::Status() {
    return pool_->Status();
}

std::string MemoryPoolServiceImpl::OutputMallocBuf() {
    return pool_->OutputMallocBuf();
}

}  // namespace cosmo::service
