#pragma once

#include <atomic>
#include <cstdint>

#include "util/ErrorCode.h"

namespace cosmo::native_face {

// Counts rejected frames for one running detector instance, not people or
// queue discards. Other inference errors do not contribute.
class OverflowCounter {
public:
    explicit OverflowCounter(util::ErrorEnum accepted = util::ErrorEnum::AI_FACE_OUTPUT_OVERFLOW)
        : accepted_(accepted) {}
    std::uint64_t Record(util::ErrorEnum status) {
        if (status != accepted_)
            return count_.load(std::memory_order_relaxed);
        return count_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    std::uint64_t Snapshot() const {
        return count_.load(std::memory_order_relaxed);
    }
    void Reset() {
        count_.store(0, std::memory_order_relaxed);
    }

private:
    const util::ErrorEnum accepted_;
    std::atomic<std::uint64_t> count_{0};
};
}  // namespace cosmo::native_face
