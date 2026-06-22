#pragma once

#include <memory>
#include <mutex>
#include <vector>

namespace cosmo::flow {

/// Reusable byte-buffer pool for MP4 recording frame transformation.
/// Reduces heap allocations during high-frequency video encoding.
///
/// Thread-safe: all methods are internally synchronized.
class BufferPool {
public:
    /// @param max_pool_size  Maximum number of idle buffers to retain.
    explicit BufferPool(size_t max_pool_size = 10);
    ~BufferPool() = default;

    BufferPool(const BufferPool&)            = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    /// Acquire a buffer with at least min_size capacity (cleared, capacity preserved).
    std::unique_ptr<std::vector<uint8_t>> AcquireBuffer(size_t min_size);

    /// Return a buffer to the pool. Dropped silently if the pool is full.
    void ReleaseBuffer(std::unique_ptr<std::vector<uint8_t>> buffer);

    /// Adjust the maximum pool size at runtime.
    void SetPoolSize(size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_pool_size_ = size;
    }

    /// Current number of idle buffers in the pool.
    [[nodiscard]] size_t GetPoolSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffers_.size();
    }

private:
    std::vector<std::unique_ptr<std::vector<uint8_t>>> buffers_;
    mutable std::mutex mutex_;
    size_t max_pool_size_;
};

}  // namespace cosmo::flow