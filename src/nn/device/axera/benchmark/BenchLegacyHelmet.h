#pragma once
// Benchmark experiment only: reproduces historical BT.601 BGR ROI, packed
// resize and direct UINT8 model bytes. Does not authorize a native frame.
#include "media/nv12_roi_bt601.h"
#include "media/nv12_roi_bt601_2rows.h"
#include "nn/device/axera/benchmark/BenchLegacyHelmetMode.h"
#include "nn/device/axera/benchmark/BenchRgbPreprocess.h"
#if defined(__aarch64__) && defined(__AARCH64EL__)
#include "nn/device/cpu/bilinear_opt.h"
#endif
#include <vector>
namespace cosmo::bench_ax {
inline bool LegacyHelmetConvert(const Nv12View& view, Rect roi, std::uint8_t* output, std::size_t bytes,
                                std::vector<std::uint8_t>& scratch) noexcept {
    if (!Valid(view) || view.width != 1920 || view.height != 1080 || view.y_stride != 2048 ||
        view.uv_stride != 2048 || !output || bytes < 224u * 224u * 3u || roi.x < 0 || roi.y < 0 ||
        roi.width <= 0 || roi.height <= 0 || roi.x > view.width - roi.width ||
        roi.y > view.height - roi.height ||
        std::size_t(roi.width) > std::numeric_limits<std::size_t>::max() / std::size_t(roi.height) / 3u)
        return false;
    try {
        scratch.resize(std::size_t(roi.width) * std::size_t(roi.height) * 3u);
        media::detail::Nv12RoiBt601ToBgr2Rows(view.y, view.y_stride, view.uv, view.uv_stride, scratch.data(),
                                              0, 0, roi.x, roi.y, roi.width, roi.height, false);
#if defined(__aarch64__) && defined(__AARCH64EL__)
        BilinearResizeNeon4x3Opt(scratch.data(), roi.width, roi.height, output, 224, 224);
#else
        const float xr = float(roi.width) / 224.f, yr = float(roi.height) / 224.f;
        for (int y = 0; y < 224; ++y) {
            const float fy = std::clamp((y + .5f) * yr - .5f, 0.f, float(roi.height - 1));
            const int sy = int(fy), sy1 = std::min(sy + 1, roi.height - 1);
            const float wy = fy - sy;
            for (int x = 0; x < 224; ++x) {
                const float fx = std::clamp((x + .5f) * xr - .5f, 0.f, float(roi.width - 1));
                const int sx = int(fx), sx1 = std::min(sx + 1, roi.width - 1);
                const float wx = fx - sx;
                for (int c = 0; c < 3; ++c) {
                    const float a = scratch[(sy * roi.width + sx) * 3 + c],
                                b = scratch[(sy * roi.width + sx1) * 3 + c],
                                d = scratch[(sy1 * roi.width + sx) * 3 + c],
                                e = scratch[(sy1 * roi.width + sx1) * 3 + c];
                    output[(y * 224 + x) * 3 + c] =
                        Byte(a * (1 - wx) * (1 - wy) + b * wx * (1 - wy) + d * (1 - wx) * wy + e * wx * wy);
                }
            }
        }
#endif
        return true;
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::bench_ax
