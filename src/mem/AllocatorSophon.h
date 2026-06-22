#pragma once

#include "mem/Allocator.h"
#include "mem/IDeviceContext.h"

namespace cosmo::mem {

class AllocatorSophon : public Allocator {
public:
    /// @param ctx Device context providing memory handle for VPU allocation.
    explicit AllocatorSophon(IDeviceContext& ctx);

    ~AllocatorSophon() override = default;

    [[nodiscard]] Block* Allocate(size_t size) override;

    void Free(Block* block) override;

private:
    IDeviceContext& ctx_;
};

}  // namespace cosmo::mem
