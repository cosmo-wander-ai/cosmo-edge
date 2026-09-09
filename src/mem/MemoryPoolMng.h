#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "mem/Allocator.h"
#include "mem/FixedBlockPool.h"
#include "util/AsyncQueue.h"
#include "util/VideoInfo.h"

namespace cosmo::mem {

struct PoolStatus {
    size_t pool_size{0};
    size_t idle_cnt{0};
    size_t used_cnt{0};
    std::vector<BlockStatus> used_nodes_status;
};

struct BlockOp {
    bool malloc{true};
    size_t size{0};
    int malloc_step{10};
    int free_step{10};
};

inline static std::vector<int> g_pool_size = {
    256 * 256 * 3 / 2,
    640 * 640 * 3 / 2,
    2073600,
    media::kVideoDefaultWidth* media::kVideoDefaultHeightAlign * 3 / 2,
    media::kVideoDefaultWidth* media::kVideoDefaultHeightAlign * 3,
    media::kVideoMaxWidth* media::kVideoMaxHeight * 3,
    media::kPictureMaxWidth* media::kPictureMaxHeight * 3 / 2};

class MemoryPoolMng {
public:
    explicit MemoryPoolMng(std::unique_ptr<Allocator> allocator_, std::vector<int> poolSize = g_pool_size);
    virtual ~MemoryPoolMng();

    [[nodiscard]] Block* Acquire(size_t size);
    void Recycle(Block* block);

    [[nodiscard]] std::vector<PoolStatus> Status() const;
    [[nodiscard]] std::string OutputMallocBuf() const;

private:
    std::shared_ptr<FixedBlockPool> CreatePoolInst(size_t size);
    [[nodiscard]] std::shared_ptr<FixedBlockPool> GetPoolInst(size_t size) const;

    void RealMalloc(BlockOp&& op);  // Allocate when pool runs low
    void RealFree(BlockOp&& op);    // Free when pool has too many idle blocks
    [[nodiscard]] size_t GetPoolSize(size_t size) const;

    void CleanAllPool();

private:
    mutable std::shared_mutex mtx_;
    std::vector<size_t> pool_block_size_;
    std::map<size_t, std::shared_ptr<FixedBlockPool>> pools_;

    // Iterate pools_ under a shared lock: keeps the map structure stable and
    // each node's shared_ptr alive for the duration of fn, mutually exclusive
    // with the exclusive writers CleanAllPool()/CreatePoolInst().
    template <typename Fn>
    void ForEachPool(Fn&& fn) const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        for (const auto& kv : pools_) {
            fn(kv.second);
        }
    }

    cosmo::AsyncQueue<BlockOp> block_op_queue_;

    std::unique_ptr<Allocator> allocator_;
};

// Infra DI Mechanism
void SetMemoryPoolContext(MemoryPoolMng* pool);
MemoryPoolMng& GetMemoryPool();

}  // namespace cosmo::mem
