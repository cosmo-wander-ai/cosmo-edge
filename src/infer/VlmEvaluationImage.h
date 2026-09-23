#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace cosmo::vlm_evaluation {

// Same float32 operation order as the frozen square-RGB reference. Padding is
// virtual to avoid allocating an intermediate square for extreme aspect ratios.
inline std::vector<uint8_t> ResizeRgb448(const uint8_t* source, size_t source_bytes, int width, int height,
                                         bool bgr) {
    if (!source || width <= 0 || height <= 0 || width > 32768 || height > 32768 ||
        static_cast<size_t>(width) * height * 3 > source_bytes)
        throw std::invalid_argument("invalid RGB image buffer or dimensions");
    const int side    = std::max(width, height);
    const int left    = (side - width) / 2;
    const int top     = (side - height) / 2;
    const float scale = static_cast<float>(side) / 448.0F;
    std::vector<uint8_t> output(448 * 448 * 3);
    const auto sample = [&](int x, int y, int c) -> float {
        if (x < left || x >= left + width || y < top || y >= top + height)
            return 128.0F;
        return source[(static_cast<size_t>(y - top) * width + x - left) * 3 + (bgr ? 2 - c : c)];
    };
    for (int y = 0; y < 448; ++y) {
        const float sy = (y + 0.5F) * scale - 0.5F;
        int y0         = static_cast<int>(std::floor(sy));
        float fy       = sy - y0;
        if (y0 < 0) {
            y0 = 0;
            fy = 0.0F;
        }
        const int y1 = std::min(y0 + 1, side - 1);
        for (int x = 0; x < 448; ++x) {
            const float sx = (x + 0.5F) * scale - 0.5F;
            int x0         = static_cast<int>(std::floor(sx));
            float fx       = sx - x0;
            if (x0 < 0) {
                x0 = 0;
                fx = 0.0F;
            }
            const int x1    = std::min(x0 + 1, side - 1);
            const float w00 = (1.0F - fx) * (1.0F - fy);
            const float w01 = fx * (1.0F - fy);
            const float w10 = (1.0F - fx) * fy;
            const float w11 = fx * fy;
            for (int c = 0; c < 3; ++c) {
                const float value = w00 * sample(x0, y0, c) + w01 * sample(x1, y0, c) +
                                    w10 * sample(x0, y1, c) + w11 * sample(x1, y1, c);
                output[(static_cast<size_t>(y) * 448 + x) * 3 + c] =
                    static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
            }
        }
    }
    return output;
}

}  // namespace cosmo::vlm_evaluation
