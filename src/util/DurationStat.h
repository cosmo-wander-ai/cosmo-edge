// Ring-buffer based duration statistics for performance profiling.
// Thread-safety: BeginSample()/EndSample() are called from a single worker
// thread; ComputeStats() may be called from any thread. Relaxed atomics are
// used for cross-thread index/counter visibility — this is acceptable because
// profiling data tolerates minor inconsistency.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cosmo::util {

using Clock = std::chrono::high_resolution_clock;

// 25fps * 60s = 1500; keep ~120 seconds of samples.
static constexpr size_t kMaxDurationSamples = 3000;

struct DurationStatInfo {
    std::string name;
    Clock::time_point start_time;
    Clock::time_point stop_time;
    int64_t duration_ns{0};      // Total duration in nanoseconds within the window.
    int64_t count{0};            // Number of samples within the window.
    int64_t duration_max_ns{0};  // Max single-sample duration within the window.
    int64_t duration_min_ns{0};  // Min single-sample duration within the window.
    int64_t cost_max_ns{0};      // All-time max single-sample duration.
    int64_t cost_min_ns{0};      // All-time min single-sample duration.
};

class DurationStat {
public:
    explicit DurationStat(std::string name);
    ~DurationStat() = default;

    // Called from the worker thread only.
    void BeginSample();
    void EndSample();

    // May be called from any thread (API / OSD thread).
    DurationStatInfo ComputeStats(int duration_ms = 30000) const;

private:
    struct Sample {
        Clock::time_point start_time;
        Clock::time_point stop_time;
        int64_t duration_ns{0};
    };

    std::string name_;
    std::vector<Sample> samples_;
    std::atomic<int64_t> index_{0};
    std::atomic<int64_t> total_count_{0};
    std::atomic<int64_t> cost_max_ns_{0};
    std::atomic<int64_t> cost_min_ns_{std::numeric_limits<int64_t>::max()};
};

}  // namespace cosmo::util
