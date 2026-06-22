// BufferPool — Buffer Pool implementation.

#include "flow/channel/BufferPool.h"

namespace cosmo::flow {

BufferPool::BufferPool(size_t max_pool_size) : max_pool_size_(max_pool_size) {}

std::unique_ptr<std::vector<uint8_t>> BufferPool::AcquireBuffer(size_t min_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unique_ptr<std::vector<uint8_t>> buffer;

    if (!buffers_.empty()) {
        // Reuse a pooled buffer
        buffer = std::move(buffers_.back());
        buffers_.pop_back();

        if (buffer->capacity() < min_size) {
            buffer->reserve(min_size);
        }
        buffer->clear();  // Clear data but preserve capacity
    } else {
        // Pool empty — allocate a new buffer
        buffer = std::make_unique<std::vector<uint8_t>>();
        buffer->reserve(min_size);
    }

    return buffer;
}

void BufferPool::ReleaseBuffer(std::unique_ptr<std::vector<uint8_t>> buffer) {
    if (!buffer) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (buffers_.size() < max_pool_size_) {
        buffer->clear();  // Clear data but preserve capacity
        buffers_.push_back(std::move(buffer));
    }
    // Pool full — buffer is released when it goes out of scope
}

}  // namespace cosmo::flow