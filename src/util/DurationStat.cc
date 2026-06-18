// Ring-buffer based duration statistics implementation.

#include "util/DurationStat.h"

#include <algorithm>

namespace cosmo::util {

DurationStat::DurationStat(std::string name) : name_(std::move(name)), samples_(kMaxDurationSamples) {}

void DurationStat::BeginSample() {
    auto idx                                      = index_.load(std::memory_order_relaxed);
    samples_[static_cast<size_t>(idx)].start_time = Clock::now();
}

void DurationStat::EndSample() {
    auto idx               = index_.load(std::memory_order_relaxed);
    auto si                = static_cast<size_t>(idx);
    samples_[si].stop_time = Clock::now();
    samples_[si].duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(samples_[si].stop_time - samples_[si].start_time)
            .count();

    int64_t cost_ns = samples_[si].duration_ns;

    // Update all-time max/min (relaxed is fine — single writer).
    if (cost_ns > cost_max_ns_.load(std::memory_order_relaxed)) {
        cost_max_ns_.store(cost_ns, std::memory_order_relaxed);
    }
    if (cost_ns < cost_min_ns_.load(std::memory_order_relaxed)) {
        cost_min_ns_.store(cost_ns, std::memory_order_relaxed);
    }

    // Advance ring buffer index (release so that reader sees the written sample).
    int64_t next = (idx + 1) % static_cast<int64_t>(kMaxDurationSamples);
    index_.store(next, std::memory_order_release);
    total_count_.fetch_add(1, std::memory_order_release);
}

DurationStatInfo DurationStat::ComputeStats(int duration_ms) const {
    // Acquire to see the latest samples written by EndSample().
    int64_t total = total_count_.load(std::memory_order_acquire);
    int64_t idx   = index_.load(std::memory_order_acquire);

    size_t count = (total > static_cast<int64_t>(kMaxDurationSamples)) ? kMaxDurationSamples
                                                                       : static_cast<size_t>(total);
    if (count == 0) {
        DurationStatInfo stat;
        stat.name = name_;
        return stat;
    }

    // Oldest element position in the ring buffer.
    size_t oldest = (total > static_cast<int64_t>(kMaxDurationSamples))
                        ? static_cast<size_t>(idx % static_cast<int64_t>(kMaxDurationSamples))
                        : 0;
    // Newest element position (exclusive).
    size_t newest = static_cast<size_t>(idx % static_cast<int64_t>(kMaxDurationSamples));

    // Find start offset within the time window.
    auto now            = Clock::now();
    size_t start_offset = 0;
    for (size_t i = 0; i < count; i++) {
        size_t si = (oldest + i) % kMaxDurationSamples;
        int64_t duration_start =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - samples_[si].start_time).count();
        int64_t duration_end =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - samples_[si].stop_time).count();
        if (((duration_start >= duration_ms) && (duration_end <= duration_ms)) ||
            (duration_start < duration_ms)) {
            start_offset = i;
            break;
        }
    }

    // Aggregate stats within the time window.
    size_t start_idx = (oldest + start_offset) % kMaxDurationSamples;
    size_t last_idx  = (newest - 1 + kMaxDurationSamples) % kMaxDurationSamples;

    DurationStatInfo stat;
    stat.start_time = samples_[start_idx].start_time;
    stat.stop_time  = samples_[last_idx].stop_time;
    int64_t total_ns{0};
    int64_t stat_count{0};
    int64_t max_ns{0};
    int64_t min_ns{std::numeric_limits<int64_t>::max()};
    for (size_t i = start_offset; i < count; i++) {
        size_t si = (oldest + i) % kMaxDurationSamples;
        total_ns += samples_[si].duration_ns;
        if (max_ns < samples_[si].duration_ns) {
            max_ns = samples_[si].duration_ns;
        }
        if (min_ns > samples_[si].duration_ns) {
            min_ns = samples_[si].duration_ns;
        }
        stat_count += 1;
    }
    stat.name            = name_;
    stat.duration_ns     = total_ns;
    stat.count           = stat_count;
    stat.duration_max_ns = max_ns;
    stat.duration_min_ns = min_ns;
    stat.cost_max_ns     = cost_max_ns_.load(std::memory_order_relaxed);
    stat.cost_min_ns     = cost_min_ns_.load(std::memory_order_relaxed);

    return stat;
}

}  // namespace cosmo::util
