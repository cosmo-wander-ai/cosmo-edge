#pragma once

#include "mem/Allocator.h"

namespace cosmo::mem {

/// CPU-backend allocator — uses standard heap memory (malloc/free).
/// Provides the same Block-based interface as AllocatorSophon but without
/// any hardware device dependency.
class AllocatorCpu : public Allocator {
public:
    AllocatorCpu() = default;

    ~AllocatorCpu() override = default;

    [[nodiscard]] Block* Allocate(size_t size) override;

    void Free(Block* block) override;
};

}  // namespace cosmo::mem
