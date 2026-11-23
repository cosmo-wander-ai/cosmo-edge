#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// CPU reference path for benchmark-only NV12 BT.709 limited to RGB input.
// The caller owns and retains both mapped planes through this call.
namespace cosmo::bench_ax {
struct Nv12View {
    const std::uint8_t* y{};
    const std::uint8_t* uv{};
    std::size_t y_bytes{}, uv_bytes{};
    int width{}, height{}, y_stride{}, uv_stride{};
};
struct Rect {
    int x{}, y{}, width{}, height{};
};
struct Mapping {
    int x{}, y{}, width{}, height{};
};
enum class Resize { Stretch, Letterbox };

inline bool Valid(const Nv12View& f) noexcept {
    if (!f.y || !f.uv || f.width <= 0 || f.height <= 0 || (f.width & 1) || (f.height & 1) ||
        f.y_stride < f.width || f.uv_stride < f.width)
        return false;
    return std::size_t(f.y_stride) <= f.y_bytes / std::size_t(f.height) &&
           std::size_t(f.uv_stride) <= f.uv_bytes / std::size_t(f.height / 2);
}
inline std::uint8_t Byte(float x) noexcept {
    return static_cast<std::uint8_t>(std::clamp(x, 0.f, 255.f) + .5f);
}
inline void Pixel(const Nv12View& f, int x, int y, float out[3]) noexcept {
    const float Y = 1.16438356f * (float(f.y[std::size_t(y) * f.y_stride + x]) - 16.f);
    const auto i  = std::size_t(y / 2) * f.uv_stride + (x & ~1);
    const float U = float(f.uv[i]) - 128.f, V = float(f.uv[i + 1]) - 128.f;
    // Round each source pixel before bilinear interpolation, like the CPU image path.
    out[0] = float(Byte(Y + 1.79274107f * V));
    out[1] = float(Byte(Y - .21324861f * U - .53290933f * V));
    out[2] = float(Byte(Y + 2.11240179f * U));
}
inline bool Convert(const Nv12View& f, Rect roi, int out_w, int out_h, Resize resize, std::uint8_t* rgb,
                    std::size_t rgb_bytes, Mapping& mapping, std::uint8_t padding = 114) noexcept {
    if (!Valid(f) || !rgb || out_w <= 0 || out_h <= 0 || roi.x < 0 || roi.y < 0 || roi.width <= 0 ||
        roi.height <= 0 || roi.x > f.width - roi.width || roi.y > f.height - roi.height ||
        std::size_t(out_w) > std::numeric_limits<std::size_t>::max() / std::size_t(out_h) / 3 ||
        rgb_bytes < std::size_t(out_w) * out_h * 3)
        return false;
    mapping = {0, 0, out_w, out_h};
    if (resize == Resize::Letterbox) {
        const float scale = std::min(float(out_w) / roi.width, float(out_h) / roi.height);
        mapping.width     = std::max(1, int(roi.width * scale));
        mapping.height    = std::max(1, int(roi.height * scale));
        mapping.x         = (out_w - mapping.width) / 2;
        mapping.y         = (out_h - mapping.height) / 2;
    }
    std::fill(rgb, rgb + std::size_t(out_w) * out_h * 3, padding);
    const float xr = float(roi.width) / mapping.width, yr = float(roi.height) / mapping.height;
    for (int dy = 0; dy < mapping.height; ++dy) {
        const float fy = std::clamp((dy + .5f) * yr - .5f, 0.f, float(roi.height - 1));
        const int sy = int(fy), sy1 = std::min(sy + 1, roi.height - 1);
        for (int dx = 0; dx < mapping.width; ++dx) {
            const float fx = std::clamp((dx + .5f) * xr - .5f, 0.f, float(roi.width - 1));
            const int sx = int(fx), sx1 = std::min(sx + 1, roi.width - 1);
            float p00[3], p01[3], p10[3], p11[3];
            const float wx = fx - sx, wy = fy - sy;
            auto* dst = rgb + (std::size_t(dy + mapping.y) * out_w + dx + mapping.x) * 3;
            Pixel(f, roi.x + sx, roi.y + sy, p00);
            if (wx == 0.f && wy == 0.f) {
                dst[0] = static_cast<std::uint8_t>(p00[0]);
                dst[1] = static_cast<std::uint8_t>(p00[1]);
                dst[2] = static_cast<std::uint8_t>(p00[2]);
                continue;
            }
            Pixel(f, roi.x + sx1, roi.y + sy, p01);
            Pixel(f, roi.x + sx, roi.y + sy1, p10);
            Pixel(f, roi.x + sx1, roi.y + sy1, p11);
            for (int c = 0; c < 3; ++c)
                dst[c] = Byte(p00[c] * (1 - wx) * (1 - wy) + p01[c] * wx * (1 - wy) + p10[c] * (1 - wx) * wy +
                              p11[c] * wx * wy);
        }
    }
    return true;
}
}  // namespace cosmo::bench_ax
