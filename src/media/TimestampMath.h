#pragma once

#include <cstdint>
#include <limits>

namespace cosmo::media {
// FFmpeg's missing-timestamp sentinel is INT64_MIN. Never subtract it, and
// reject a difference that cannot be represented in the destination type.
inline bool TryTimestampDifference(int64_t value, int64_t previous, int64_t& delta) {
    constexpr auto min = std::numeric_limits<int64_t>::min();
    constexpr auto max = std::numeric_limits<int64_t>::max();
    if (value == min || previous == min || (previous > 0 && value < min + previous) ||
        (previous < 0 && value > max + previous))
        return false;
    delta = value - previous;
    return true;
}
}  // namespace cosmo::media
