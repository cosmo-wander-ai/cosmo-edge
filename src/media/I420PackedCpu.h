#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cosmo::media::detail {

// Matches the existing CPU BT.601 limited-range integer conversion. The
// caller owns distinct, tightly packed I420 input and RGB/BGR output buffers.
template <bool Bgr>
inline bool I420ToPacked(const uint8_t* src, uint8_t* dst, int width, int height) {
    if (!src || !dst || width <= 0 || height <= 0 || (width & 1) || (height & 1))
        return false;
    const size_t w = static_cast<size_t>(width), h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h || w * h > std::numeric_limits<size_t>::max() / 3)
        return false;
    const size_t pixels    = w * h;
    const uint8_t* const y = src;
    const uint8_t* const u = src + pixels;
    const uint8_t* const v = u + pixels / 4;
    const auto clip        = [](int x) { return static_cast<uint8_t>(std::clamp(x, 0, 255)); };
    for (size_t row = 0; row < h; row += 2) {
        for (size_t col = 0; col < w; col += 2) {
            const size_t chroma = (row / 2) * (w / 2) + col / 2;
            const int d         = static_cast<int>(u[chroma]) - 128;
            const int e         = static_cast<int>(v[chroma]) - 128;
            const int red       = 409 * e + 128;
            const int green     = -100 * d - 208 * e + 128;
            const int blue      = 516 * d + 128;
            for (size_t dy = 0; dy < 2; ++dy) {
                const size_t offset = (row + dy) * w + col;
                for (size_t dx = 0; dx < 2; ++dx) {
                    const int lum   = 298 * (static_cast<int>(y[offset + dx]) - 16);
                    const uint8_t r = clip((lum + red) >> 8);
                    const uint8_t g = clip((lum + green) >> 8);
                    const uint8_t b = clip((lum + blue) >> 8);
                    auto* pixel     = dst + 3 * (offset + dx);
                    pixel[0]        = Bgr ? b : r;
                    pixel[1]        = g;
                    pixel[2]        = Bgr ? r : b;
                }
            }
        }
    }
    return true;
}
}  // namespace cosmo::media::detail
