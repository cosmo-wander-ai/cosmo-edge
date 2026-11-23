#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

namespace cosmo {

// Owned by one classifier action instance; counters remain monotonic even if
// its model object is reinitialized. Counts represent calls, not target boxes.
struct ClassifierExecutionCounters {
    std::atomic<uint64_t> forward_success{0};
    std::atomic<uint64_t> forward_failure{0};
    std::atomic<uint64_t> empty_candidates{0};
    // Includes non-forward errors and processing while an earlier error remains
    // the node status; it is deliberately conservative when no call was made.
    std::atomic<uint64_t> non_forward_failure{0};

    void RecordProcessingOutcome(uint64_t before_success, uint64_t before_failure, bool success) noexcept {
        const auto after_success = forward_success.load(std::memory_order_relaxed);
        const auto after_failure = forward_failure.load(std::memory_order_relaxed);
        if (!success && after_failure == before_failure)
            non_forward_failure.fetch_add(1, std::memory_order_relaxed);
        else if (success && after_success == before_success && after_failure == before_failure)
            empty_candidates.fetch_add(1, std::memory_order_relaxed);
    }

    template <typename Callable>
    auto RunForward(Callable&& call) {
        try {
            auto result = std::forward<Callable>(call)();
            (static_cast<bool>(result) ? forward_success : forward_failure)
                .fetch_add(1, std::memory_order_relaxed);
            return result;
        } catch (...) {
            forward_failure.fetch_add(1, std::memory_order_relaxed);
            throw;
        }
    }
};

}  // namespace cosmo
