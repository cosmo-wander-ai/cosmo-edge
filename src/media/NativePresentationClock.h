#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

// Private candidate: an individual clock belongs to one stream generation.
// PTS is allowed to arrive out of presentation order, as with H.264 B-frames.
namespace cosmo::native_presentation {
// Check the exact float arithmetic used by local-file pacing before any
// integer multiplication or floating-to-integer conversion can overflow.
inline bool NativeFpsControlSafe(std::int64_t index, float fps) noexcept {
    if (index < 0 || !std::isfinite(fps))
        return false;
    if (fps <= 0.0f)
        return true;  // legacy pacing is skipped
    if (index > std::numeric_limits<std::int64_t>::max() / 1000)
        return false;
    const float milliseconds = static_cast<float>(index * 1000) / fps;
    if (!std::isfinite(milliseconds) || milliseconds < 0.0f ||
        static_cast<long double>(milliseconds) >
            static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return false;
    const auto value     = static_cast<std::int64_t>(milliseconds);
    using Duration       = std::chrono::steady_clock::duration;
    using Rep            = Duration::rep;
    const __int128 ticks = static_cast<__int128>(value) * Duration::period::den /
                           (static_cast<__int128>(1000) * Duration::period::num);
    return ticks >= std::numeric_limits<Rep>::min() && ticks <= std::numeric_limits<Rep>::max();
}

struct FrameTime {
    std::int64_t wall_ms;
    std::int64_t steady_ms;
};

class PresentationClock {
public:
    static constexpr std::int64_t kNoPts = std::numeric_limits<std::int64_t>::min();

    bool Initialize(std::int64_t first_pts, std::int32_t time_base_num, std::int32_t time_base_den,
                    std::int64_t wall_ms, std::int64_t steady_ms, std::int64_t drift_limit_ms) {
        Reset();
        if (first_pts == kNoPts || time_base_num <= 0 || time_base_den <= 0 || drift_limit_ms < 0)
            return false;
        pts_         = first_pts;
        num_         = time_base_num;
        den_         = time_base_den;
        wall_        = wall_ms;
        steady_      = steady_ms;
        drift_       = drift_limit_ms;
        initialized_ = true;
        return true;
    }

    std::optional<FrameTime> Convert(std::int64_t pts, std::int64_t arrival_steady_ms) const {
        if (!initialized_ || pts == kNoPts)
            return std::nullopt;
        // The difference is computed after widening; multiplying the largest
        // signed 64-bit difference by INT32_MAX and 1000 fits signed 128-bit.
        const __int128 ticks    = static_cast<__int128>(pts) - static_cast<__int128>(pts_);
        const __int128 delta_ms = ticks * static_cast<__int128>(num_) * 1000 / den_;
        const __int128 wall     = static_cast<__int128>(wall_) + delta_ms;
        const __int128 steady   = static_cast<__int128>(steady_) + delta_ms;
        constexpr auto min      = std::numeric_limits<std::int64_t>::min();
        constexpr auto max      = std::numeric_limits<std::int64_t>::max();
        if (wall < min || wall > max || steady < min || steady > max)
            return std::nullopt;
        const __int128 difference = steady - static_cast<__int128>(arrival_steady_ms);
        if (difference < -static_cast<__int128>(drift_) || difference > static_cast<__int128>(drift_))
            return std::nullopt;
        return FrameTime{static_cast<std::int64_t>(wall), static_cast<std::int64_t>(steady)};
    }

    void Reset() {
        initialized_ = false;
        pts_         = 0;
        num_ = den_ = 0;
        wall_ = steady_ = drift_ = 0;
    }

private:
    bool initialized_{false};
    std::int64_t pts_{0};
    std::int32_t num_{0};
    std::int32_t den_{0};
    std::int64_t wall_{0};
    std::int64_t steady_{0};
    std::int64_t drift_{0};
};
}  // namespace cosmo::native_presentation
