// Device memory pool manager.
// Manages fixed-size block pools_ backed by VPU device memory.

#include "mem/MemoryPoolMng.h"

#include <algorithm>
#include <mutex>

#include "util/Log.h"

namespace cosmo::mem {

static MemoryPoolMng* g_memoryPoolMng = nullptr;

void SetMemoryPoolContext(MemoryPoolMng* pool) {
    g_memoryPoolMng = pool;
}

MemoryPoolMng& GetMemoryPool() {
    if (!g_memoryPoolMng) {
        LOG_ERRO("{}", "FATAL: MemoryPoolMng not initialized! Call SetMemoryPoolContext() in app_init.");
        std::abort();
    }
    return *g_memoryPoolMng;
}

MemoryPoolMng::MemoryPoolMng(std::unique_ptr<Allocator> alloc, std::vector<int> poolSize)
    : block_op_queue_("Block Malloc", 20), allocator_(std::move(alloc)) {
    // Sort pool sizes ascending for GetPoolSize() binary search
    sort(poolSize.begin(), poolSize.end(), [](const auto& p1, const auto& p2) { return p1 < p2; });
    pool_block_size_ = poolSize;

    for (auto size : pool_block_size_) {
        auto pool = CreatePoolInst(size);
        if (!pool) {
            LOG_WARN("[POOL:{}] Create Failed.", size);
            continue;
        }
    }

    block_op_queue_.SetProcessor([this](BlockOp&& data) {
        if (data.malloc) {
            RealMalloc(std::move(data));
        } else {
            RealFree(std::move(data));
        }
    });
}

MemoryPoolMng::~MemoryPoolMng() {
    LOG_INFO("{}", "MemoryPoolMng Stop");
    block_op_queue_.Stop();
    LOG_INFO("{}", "MemoryPoolMng Clean Pool");
    CleanAllPool();

    allocator_.reset();
    LOG_INFO("{}", "MemoryPoolMng Quit");
}

void MemoryPoolMng::CleanAllPool() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto it = pools_.begin(); it != pools_.end();) {
        while (1) {
            auto pool  = it->second;
            auto block = pool->DeleteBlock();
            if (block) {
                allocator_->Free(block);
            } else {
                break;
            }
        }
        it++;
    }
    pools_.clear();
}

void MemoryPoolMng::RealMalloc(BlockOp&& op) {
    auto size         = op.size;
    int step          = op.malloc_step;
    int successBlocks = 0;

    if (size == 0) {
        return;
    }

    // get pool
    auto pool = GetPoolInst(static_cast<int>(op.size));
    if (!pool) {
        LOG_WARN("Malloc {} for {} Blocks But Cant Get Pool Inst", size, step);
        return;
    }

    // Throttle: skip if idle count already above threshold
    if (pool->IdleCount() > pool->GetMallocThres()) {
        return;
    }

    if ((pool->GetMaxLimit() > 0) && (pool->TotalBlockCount() >= pool->GetMaxLimit())) {
        freq_calc_.MallocFull();
        return;
    }

    for (int i = 0; i < step; i++) {
        auto block_ = allocator_->Allocate(size);
        if (block_) {
            successBlocks += 1;
            pool->AddBlock(block_);
        } else {
            freq_calc_.MallocFail();
            LOG_WARN("bm_malloc_device_byte {} failed, FREE:{} TOTAL:{}", size, pool->IdleCount(),
                     pool->TotalBlockCount());
            return;
        }
    }

    if (successBlocks != step) {
        LOG_WARN("[POOL:{}] Add {} Blocks, Success {} Blocks, Now(Using/Free):{}/{}", pool->BlockSize(), step,
                 successBlocks, pool->UsingCount(), pool->IdleCount());
    } else {
        LOG_INFO("[POOL:{}] Add {} Blocks, Now(Using/Free):{}/{}", pool->BlockSize(), step,
                 pool->UsingCount(), pool->IdleCount());
    }
}

void MemoryPoolMng::RealFree(BlockOp&& op) {
    auto size         = op.size;
    int step          = op.free_step;
    int successBlocks = 0;

    if (size == 0) {
        return;
    }

    // get pool
    auto pool = GetPoolInst(static_cast<int>(op.size));
    if (!pool) {
        LOG_WARN("Free {} for {} Blocks But Cant Get Pool Inst", size, step);
        return;
    }

    // Throttle: skip if idle count already below threshold
    if (pool->IdleCount() <= pool->GetFreeThres()) {
        return;
    }

    if (pool->TotalBlockCount() <= pool->GetMinLimit()) {
        return;
    }

    for (int i = 0; i < step; i++) {
        auto block = pool->DeleteBlock();
        if (block) {
            successBlocks += 1;
            allocator_->Free(block);
        }
    }

    if (successBlocks != step) {
        LOG_WARN("[POOL:{}] Delete {} Blocks, Success {} Blocks, Now(Using/Free):{}/{}", pool->BlockSize(),
                 step, successBlocks, pool->UsingCount(), pool->IdleCount());
    } else {
        LOG_INFO("[POOL:{}] Delete {} Blocks, Now(Using/Free):{}/{}", pool->BlockSize(), step,
                 pool->UsingCount(), pool->IdleCount());
    }
}

int MemoryPoolMng::GetPoolSize(int size) const {
    auto it = std::find_if(pool_block_size_.begin(), pool_block_size_.end(),
                           [size](int poolSize) { return poolSize >= size; });
    if (it != pool_block_size_.end()) {
        return *it;
    }
    LOG_WARN("size:{} is Not Init in Pool", size);
    return 0;
}

std::shared_ptr<FixedBlockPool> MemoryPoolMng::CreatePoolInst(int poolSize) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = pools_.find(poolSize);
    if (it == pools_.end()) {
        auto pool = std::make_shared<FixedBlockPool>(poolSize);
        pools_.emplace(poolSize, pool);
        return pool;
    }

    return nullptr;
}

std::shared_ptr<FixedBlockPool> MemoryPoolMng::GetPoolInst(int size) const {
    int poolSize = GetPoolSize(size);
    if (0 == poolSize) {
        return nullptr;
    }

    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = pools_.find(poolSize);
    if (it == pools_.end()) {
        return nullptr;
    }
    return it->second;
}

Block* MemoryPoolMng::Acquire(size_t size) {
    auto pool = GetPoolInst(static_cast<int>(size));
    if (!pool) {
        LOG_WARN("Malloc {} But Cant Get Pool Inst", size);
        return nullptr;
    }

    auto block = pool->AcquireIdleBlock();

    // After acquire, check if idle count is below threshold and request more
    if (pool->IdleCount() <= pool->GetMallocThres()) {
        BlockOp op;
        op.malloc      = true;  // Request allocation
        op.size        = pool->BlockSize();
        op.malloc_step = pool->GetMallocStep();
        op.free_step   = pool->GetFreeStep();
        block_op_queue_.Insert(op);
    }

    return block;
}

void MemoryPoolMng::Recycle(Block* block) {
    if (!block) {
        return;
    }

    auto size = block->size;
    if (size == 0)
        return;

    auto pool = GetPoolInst(static_cast<int>(size));
    if (!pool) {
        LOG_WARN("Recycle {} But Cant Get Pool Inst", size);
        return;
    }
    pool->RecycleBlock(block);

    // Periodically print pool status, assist in diagnosing if RealFree is triggered
    static thread_local uint64_t recycle_counter = 0;
    if (0 == (++recycle_counter % 100)) {
        LOG_INFO("[POOL:{}] Recycle#{} Using:{} Idle:{} Total:{} FreeThres:{} MinLimit:{}{}",
                 pool->BlockSize(), recycle_counter, pool->UsingCount(), pool->IdleCount(),
                 pool->TotalBlockCount(), pool->GetFreeThres(), pool->GetMinLimit(),
                 pool->IdleCount() > pool->GetFreeThres() ? " -> TriggerFree" : "");
    }

    if (pool->IdleCount() > pool->GetFreeThres()) {
        BlockOp op;
        op.malloc      = false;  // Request deallocation
        op.size        = pool->BlockSize();
        op.malloc_step = pool->GetMallocStep();
        op.free_step   = pool->GetFreeStep();
        block_op_queue_.Insert(op);
    }
}

std::vector<PoolStatus> MemoryPoolMng::Status() const {
    std::vector<PoolStatus> status;
    for (auto it = pools_.begin(); it != pools_.end();) {
        PoolStatus statu;
        statu.pool_size         = it->second->BlockSize();
        statu.used_cnt          = it->second->UsingCount();
        statu.idle_cnt          = it->second->IdleCount();
        statu.used_nodes_status = it->second->Status();
        status.push_back(statu);
        it++;
    }
    return status;
}

std::string MemoryPoolMng::OutputMallocBuf() const {
    std::string msg;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto it = pools_.begin(); it != pools_.end();) {
        msg += it->second->OutputMalloc();
        msg += "\\n";
        it++;
    }
    return msg;
}

}  // namespace cosmo::mem