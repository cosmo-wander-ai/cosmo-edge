#pragma once

#include <list>
#include <shared_mutex>
#include <string>
#include <vector>

#include "mem/Block.h"

namespace cosmo::mem {

class FixedBlockPool {
public:
    explicit FixedBlockPool(size_t size);
    ~FixedBlockPool();

    [[nodiscard]] size_t BlockSize() const;

    [[nodiscard]] size_t UsingCount() const;
    [[nodiscard]] size_t IdleCount() const;

    [[nodiscard]] size_t TotalBlockCount() const;

    [[nodiscard]] size_t GetMallocStep() const;

    [[nodiscard]] size_t GetFreeStep() const;

    [[nodiscard]] size_t GetMallocThres() const;

    [[nodiscard]] size_t GetFreeThres() const;

    [[nodiscard]] size_t GetMaxLimit() const;

    [[nodiscard]] size_t GetMinLimit() const;

    Block* AcquireIdleBlock();
    void RecycleBlock(Block* b);

    void AddBlock(Block*);
    Block* DeleteBlock();

    [[nodiscard]] std::vector<BlockStatus> Status() const;

    [[nodiscard]] std::string OutputMalloc() const;

private:
    [[nodiscard]] bool CheckBlock(Block*) const;

private:
    size_t block_size_ = 0;  // Pool memory specification

    mutable std::shared_mutex mtx_;

    /**
     * Number of blocks to allocate/free at once
     */
    size_t malloc_step_ = 20;
    size_t free_step_   = 10;

    /**
     * Threshold for triggering memory allocation/free of idle blocks
     */
    size_t malloc_thres_ = 10;
    size_t free_thres_   = 50;

    size_t max_limit_ = 80;
    size_t min_limit_ = 10;

    std::list<Block*> used_blocks_;
    std::list<Block*> idle_blocks_;
};

}  // namespace cosmo::mem
