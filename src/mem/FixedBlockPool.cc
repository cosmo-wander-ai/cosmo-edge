// FixedBlockPool — Fixed Block Pool implementation.

#include "mem/FixedBlockPool.h"

#include <syscall.h>

#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/VideoInfo.h"

namespace cosmo::mem {

FixedBlockPool::~FixedBlockPool() {
    if ((used_blocks_.size() > 0) || (idle_blocks_.size() > 0))
        LOG_WARN("=== Size {} Pool  UsingCount:{} FreeCount:{}===", block_size_, used_blocks_.size(),
                 idle_blocks_.size());
    LOG_INFO("=== Size {} Pool Quit", block_size_);
}

FixedBlockPool::FixedBlockPool(size_t size) {
    // Apply for a smaller quantity at once if the size is too large
    if (size > media::kVideo4KWidth * media::kVideo4KWidth * 3) {
        malloc_step_  = 3;
        free_step_    = 1;
        malloc_thres_ = 8;
        free_thres_   = 20;
    } else if (size == media::kVideoDefaultWidth * media::kVideoDefaultHeightAlign * 3 / 2) {
        malloc_step_  = 10;
        free_step_    = 8;
        malloc_thres_ = 10;
        free_thres_   = 20;
        max_limit_    = 200;
        min_limit_    = 30;
    } else if (size == media::kVideoDefaultWidth * media::kVideoDefaultHeightAlign * 3) {
        malloc_step_  = 10;
        free_step_    = 8;
        malloc_thres_ = 10;
        free_thres_   = 60;
        max_limit_    = 400;
        min_limit_    = 64;
    } else if (size > media::kVideoDefaultWidth * media::kVideoDefaultWidth * 3) {
        malloc_step_  = 10;
        free_step_    = 3;
        malloc_thres_ = 8;
        free_thres_   = 20;
    }

    block_size_ = size;
    LOG_INFO("=== Size {} Pool Init", block_size_);
}

size_t FixedBlockPool::BlockSize() const {
    return block_size_;
}

size_t FixedBlockPool::GetMallocStep() const {
    return malloc_step_;
}

size_t FixedBlockPool::GetFreeStep() const {
    return free_step_;
}

size_t FixedBlockPool::GetMallocThres() const {
    return malloc_thres_;
}

size_t FixedBlockPool::GetFreeThres() const {
    return free_thres_;
}

size_t FixedBlockPool::GetMaxLimit() const {
    return max_limit_;
}

size_t FixedBlockPool::GetMinLimit() const {
    return min_limit_;
}

size_t FixedBlockPool::UsingCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return used_blocks_.size();
}

size_t FixedBlockPool::IdleCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return idle_blocks_.size();
}

size_t FixedBlockPool::TotalBlockCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return idle_blocks_.size() + used_blocks_.size();
}

Block* FixedBlockPool::AcquireIdleBlock() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (idle_blocks_.empty()) {
        LOG_INFO("Pool:{} Step:{} {} Rest:{} Max:{} LimitMax:{} LimitMin:{} Malloc:{} Free:{}", block_size_,
                 malloc_step_, free_step_, malloc_thres_, free_thres_, max_limit_, min_limit_,
                 used_blocks_.size(), idle_blocks_.size());
        return nullptr;
    }

    auto block = std::move(idle_blocks_.front());
    idle_blocks_.pop_front();

    block->status.malloc_timepoint = util::GetMilliseconds();
    block->status.thread_id        = static_cast<pid_t>(syscall(__NR_gettid));
    block->status.duration         = 0;

    used_blocks_.push_back(block);
    return block;
}

void FixedBlockPool::RecycleBlock(Block* b) {
    if (!CheckBlock(b))
        return;

    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto iter = std::find_if(used_blocks_.begin(), used_blocks_.end(),
                             [&](const Block* node) { return node->data == b->data; });
    if (iter != used_blocks_.end()) {
        auto node = (*iter);
        used_blocks_.erase(iter);
        idle_blocks_.push_back(node);
    } else {
        LOG_WARN("[POOL:{}] Recycle Failed. Using/Free:{}/{}", block_size_, used_blocks_.size(),
                 idle_blocks_.size());
    }
}

bool FixedBlockPool::CheckBlock(Block* block) const {
    if (!block)
        return false;

    if (block->size == 0)
        return false;

    if (!block->data)
        return false;

    return true;
}

void FixedBlockPool::AddBlock(Block* b) {
    if (!CheckBlock(b))
        return;

    std::lock_guard<std::shared_mutex> lock(mtx_);
    idle_blocks_.push_back(b);
}

Block* FixedBlockPool::DeleteBlock() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (idle_blocks_.empty()) {
        return nullptr;
    }

    auto block = std::move(idle_blocks_.front());
    idle_blocks_.pop_front();

    return block;
}

std::vector<BlockStatus> FixedBlockPool::Status() const {
    std::vector<BlockStatus> nodes;
    auto timepoint = util::GetMilliseconds();
    std::shared_lock<std::shared_mutex> lock(mtx_);
    LOG_INFO("=== Size {} Pool  Using:{} Idle:{}===", block_size_, used_blocks_.size(), idle_blocks_.size());
    for (auto& b : used_blocks_) {
        b->status.duration = timepoint - b->status.malloc_timepoint;
        nodes.push_back(b->status);
    }

    return nodes;
}

std::string FixedBlockPool::OutputMalloc() const {
    std::string msg;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    LOG_INFO("=== Size {} Pool  Using:{} Idle:{}===", block_size_, used_blocks_.size(), idle_blocks_.size());
    msg = "=== Size:" + std::to_string(block_size_) + " Pool Using:" + std::to_string(used_blocks_.size()) +
          " Idle:" + std::to_string(idle_blocks_.size()) + " ===";
    return msg;
}

}  // namespace cosmo::mem