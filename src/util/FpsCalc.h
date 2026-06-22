// Frame rate calculator based on a sliding interval.
// Computes FPS by measuring elapsed time over a configurable number of frames.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace cosmo::util {

class FpsCalc {
public:
    using Clock = std::chrono::high_resolution_clock;

    static constexpr size_t kDefaultInterval = 25;
    static constexpr size_t kMinInterval     = 10;

    explicit FpsCalc(size_t interval = kDefaultInterval) : interval_(std::max(interval, kMinInterval)) {}

    ~FpsCalc() = default;

    void Reset();

    // Calculate and return current FPS.
    // Must be called once per frame to maintain accuracy.
    [[nodiscard]] float Fps();

    // Return both the cumulative frame count and current FPS.
    [[nodiscard]] std::pair<size_t, float> FpsWithFrame();

private:
    size_t interval_{kDefaultInterval};
    size_t frame_index_{0};
    float fps_{0.0f};
    Clock::time_point start_time_{Clock::now()};
};

}  // namespace cosmo::util
