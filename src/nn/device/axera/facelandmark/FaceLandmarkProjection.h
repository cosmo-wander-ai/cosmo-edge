#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
namespace cosmo::face_landmark_candidate {
struct Point {
    float x;
    float y;
};
using Points = std::array<Point, 5>;
inline std::optional<Points> Project(const std::array<std::int32_t, 4>& roi,
                                     const std::array<float, 10>& xy) noexcept {
    if (roi[0] < 0 || roi[1] < 0 || roi[2] > 1920 || roi[3] > 1080 || roi[0] >= roi[2] || roi[1] >= roi[3])
        return {};
    Points out{};
    const float w = static_cast<float>(roi[2] - roi[0]), h = static_cast<float>(roi[3] - roi[1]);
    for (std::size_t i = 0; i < 5; ++i) {
        const auto x = xy[2 * i], y = xy[2 * i + 1];
        if (!std::isfinite(x) || !std::isfinite(y))
            return {};
        out[i] = {roi[0] + x * w, roi[1] + y * h};
        if (!std::isfinite(out[i].x) || !std::isfinite(out[i].y))
            return {};
    }
    return out;
}
}  // namespace cosmo::face_landmark_candidate
