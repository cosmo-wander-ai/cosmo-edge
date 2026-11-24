#pragma once

#include "nn/utils/rect.h"

namespace cosmo::nn {

// Host and Rockchip resize truncate the scaled extent before placing it.
inline Rect CalculateResizeRegion(int source_width, int source_height, int target_width, int target_height,
                                  int gravity) {
    if (source_width <= 0 || source_height <= 0 || target_width <= 0 || target_height <= 0)
        return {};
    if (gravity == 0)
        return {0, 0, target_width, target_height};

    const float scale = std::min(static_cast<float>(target_width) / source_width,
                                 static_cast<float>(target_height) / source_height);
    const int width   = static_cast<int>(source_width * scale);
    const int height  = static_cast<int>(source_height * scale);
    const int x       = gravity == 1 ? (target_width - width) / 2 : 0;
    const int y       = gravity == 1 ? (target_height - height) / 2 : 0;
    return {x, y, width, height};
}

}  // namespace cosmo::nn
