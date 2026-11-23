#pragma once

#include <cmath>

namespace cosmo::media {

inline bool NativeLiveFrameRateUnknown(bool strict_native, bool live, bool has_metadata,
                                       float selected_rate) {
    return strict_native && live &&
           (!has_metadata || !std::isfinite(selected_rate) || selected_rate <= 0 || selected_rate > 1000);
}

}  // namespace cosmo::media
