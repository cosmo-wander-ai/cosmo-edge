#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace cosmo::media {
// Convert a mapped millisecond count to the actual steady-clock representation
// without overflow or silent precision loss.
inline std::optional<std::chrono::steady_clock::time_point> NativeSteadyTimeFromMs(int64_t ms) noexcept {
    using Clock              = std::chrono::steady_clock;
    using Duration           = Clock::duration;
    using Rep                = Duration::rep;
    const __int128 numerator = static_cast<__int128>(ms) * Duration::period::den;
    const __int128 divisor   = static_cast<__int128>(1000) * Duration::period::num;
    if (numerator % divisor != 0)
        return std::nullopt;
    const __int128 ticks = numerator / divisor;
    if (ticks < std::numeric_limits<Rep>::min() || ticks > std::numeric_limits<Rep>::max())
        return std::nullopt;
    return Clock::time_point{Duration{static_cast<Rep>(ticks)}};
}

// Checks decoded presentation order, not compressed packet arrival order.
// A failed generation stays closed until a strictly newer stream generation.
class NativePresentationOrder final {
public:
    bool Accept(uint64_t generation, int64_t pts, int64_t wall_ms, int64_t steady_ms) noexcept {
        if (!generation)
            return false;
        if (generation != generation_) {
            if (generation < generation_)
                return false;
            generation_  = generation;
            initialized_ = false;
            failed_      = false;
        }
        if (failed_)
            return false;
        if (pts == std::numeric_limits<int64_t>::min()) {
            failed_ = true;
            return false;
        }
        if (initialized_ && (pts <= pts_ || wall_ms < wall_ms_ || steady_ms < steady_ms_)) {
            failed_ = true;
            return false;
        }
        pts_         = pts;
        wall_ms_     = wall_ms;
        steady_ms_   = steady_ms;
        initialized_ = true;
        return true;
    }
    void Reset() noexcept {
        generation_  = 0;
        initialized_ = false;
        failed_      = false;
    }

private:
    uint64_t generation_{0};
    int64_t pts_{0}, wall_ms_{0}, steady_ms_{0};
    bool initialized_{false}, failed_{false};
};
}  // namespace cosmo::media
