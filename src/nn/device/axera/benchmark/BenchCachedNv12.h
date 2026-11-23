#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "BenchRgbPreprocess.h"

namespace cosmo::bench_ax {
// Retain the exact plane bytes, geometry and stride for the duration of a frame.
// The caller retains the mapped source and its witness while Capture runs.
class Nv12Snapshot {
public:
    bool Capture(const Nv12View& source) noexcept {
        view_ = {};
        if (!Valid(source))
            return false;
        try {
            y_.resize(source.y_bytes);
            uv_.resize(source.uv_bytes);
        } catch (...) {
            return false;
        }
        std::memcpy(y_.data(), source.y, source.y_bytes);
        std::memcpy(uv_.data(), source.uv, source.uv_bytes);
        view_    = source;
        view_.y  = y_.data();
        view_.uv = uv_.data();
        return true;
    }
    // Convert reads only the selected RGB ROI pixels and their paired UV
    // samples. Keep the original stride/layout; all other bytes are unused.
    // An invalid ROI or a workload no cheaper than full copying follows the
    // original path so its existing validation/failure ordering is preserved.
    bool CaptureRegions(const Nv12View& source,
                        const std::vector<std::array<std::int32_t, 4>>& rois) noexcept {
        view_ = {};
        if (!Valid(source))
            return false;
        if (rois.empty())
            return Capture(source);
        const auto max = std::numeric_limits<std::size_t>::max();
        if (source.y_bytes > max - source.uv_bytes)
            return Capture(source);
        const auto full   = source.y_bytes + source.uv_bytes;
        std::size_t bytes = 0;
        for (const auto& r : rois) {
            if (r[0] < 0 || r[1] < 0 || r[2] <= r[0] || r[3] <= r[1] || r[2] > source.width ||
                r[3] > source.height)
                return Capture(source);
            const std::size_t w   = static_cast<std::size_t>(r[2] - r[0]),
                              h   = static_cast<std::size_t>(r[3] - r[1]);
            const std::size_t ux0 = static_cast<std::size_t>(r[0]) & ~std::size_t{1};
            const std::size_t ux1 = ((static_cast<std::size_t>(r[2]) - 1) & ~std::size_t{1}) + 2;
            const std::size_t uy0 = static_cast<std::size_t>(r[1] / 2),
                              uy1 = static_cast<std::size_t>((r[3] - 1) / 2 + 1);
            if (w > max / h || ux1 - ux0 > max / (uy1 - uy0))
                return Capture(source);
            const auto ybytes = w * h, uvbytes = (ux1 - ux0) * (uy1 - uy0);
            if (ybytes > max - uvbytes)
                return Capture(source);
            const auto add = ybytes + uvbytes;
            if (add >= full - bytes)
                return Capture(source);
            bytes += add;
        }
        try {
            y_.resize(source.y_bytes);
            uv_.resize(source.uv_bytes);
        } catch (...) {
            return false;
        }
        for (const auto& r : rois) {
            const auto x = static_cast<std::size_t>(r[0]), w = static_cast<std::size_t>(r[2] - r[0]);
            for (int row = r[1]; row < r[3]; ++row) {
                const auto pos = static_cast<std::size_t>(row) * source.y_stride + x;
                std::memcpy(y_.data() + pos, source.y + pos, w);
            }
            const auto ux0 = static_cast<std::size_t>(r[0]) & ~std::size_t{1};
            const auto ux1 = ((static_cast<std::size_t>(r[2]) - 1) & ~std::size_t{1}) + 2;
            for (int row = r[1] / 2; row <= (r[3] - 1) / 2; ++row) {
                const auto pos = static_cast<std::size_t>(row) * source.uv_stride + ux0;
                std::memcpy(uv_.data() + pos, source.uv + pos, ux1 - ux0);
            }
        }
        view_    = source;
        view_.y  = y_.data();
        view_.uv = uv_.data();
        return true;
    }
    Nv12View View() const noexcept {
        return view_;
    }

private:
    std::vector<std::uint8_t> y_, uv_;
    Nv12View view_{};
};
}  // namespace cosmo::bench_ax
