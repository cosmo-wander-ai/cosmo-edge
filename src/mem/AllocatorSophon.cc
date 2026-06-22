// AllocatorSophon — Allocator Sophon implementation.

#include "mem/AllocatorSophon.h"

#include <limits>
#include <memory>

#include "bmlib_runtime.h"

namespace cosmo::mem {

AllocatorSophon::AllocatorSophon(IDeviceContext& ctx) : Allocator(), ctx_(ctx) {}

Block* AllocatorSophon::Allocate(size_t size) {
    if (size == 0)
        return nullptr;

    auto handle = reinterpret_cast<bm_handle_t>(ctx_.GetMemoryHandle());

    // RAII: unique_ptr manages bm_device_mem_t lifetime instead of raw malloc/free.
    auto device_mem = std::make_unique<bm_device_mem_t>();

    if (size > std::numeric_limits<unsigned int>::max()) {
        return nullptr;
    }

    auto ret = bm_malloc_device_byte_heap(handle, device_mem.get(), 1, static_cast<unsigned int>(size));
    if (ret != bm_status_t::BM_SUCCESS) {
        return nullptr;
    }

    // RAII: unique_ptr manages Block lifetime; release() transfers ownership
    // to the caller (FixedBlockPool) which manages the raw pointer in its pool.
    auto block  = std::make_unique<Block>();
    block->data = reinterpret_cast<uint8_t*>(device_mem.release());
    block->size = size;

    return block.release();
}

void AllocatorSophon::Free(Block* block) {
    if (!block)
        return;

    // RAII: unique_ptr ensures Block is always deleted, even if early return.
    auto block_guard = std::unique_ptr<Block>(block);

    if (block_guard->data) {
        // RAII: unique_ptr ensures bm_device_mem_t is always freed.
        auto dev_mem =
            std::unique_ptr<bm_device_mem_t>(reinterpret_cast<bm_device_mem_t*>(block_guard->data));
        auto handle = reinterpret_cast<bm_handle_t>(ctx_.GetMemoryHandle());
        bm_free_device(handle, *dev_mem);
        // dev_mem automatically deleted by unique_ptr destructor
    }

    // block_guard automatically deleted by unique_ptr destructor
}

}  // namespace cosmo::mem