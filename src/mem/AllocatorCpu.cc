// AllocatorCpu — Allocator Cpu implementation.

#include "mem/AllocatorCpu.h"

#include <cstdlib>
#include <memory>

namespace cosmo::mem {

Block* AllocatorCpu::Allocate(size_t size) {
    if (size == 0)
        return nullptr;

    auto* data = static_cast<uint8_t*>(std::malloc(size));
    if (!data)
        return nullptr;

    auto block  = std::make_unique<Block>();
    block->data = data;
    block->size = size;

    return block.release();
}

void AllocatorCpu::Free(Block* block) {
    if (!block)
        return;

    auto block_guard = std::unique_ptr<Block>(block);

    if (block_guard->data) {
        std::free(block_guard->data);
    }
}

}  // namespace cosmo::mem
