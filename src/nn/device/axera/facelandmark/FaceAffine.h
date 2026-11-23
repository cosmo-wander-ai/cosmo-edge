#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "FaceLandmarkProjection.h"
namespace cosmo::face_landmark_candidate {
// Reproduces the reference feature pipeline's 112x112, norm-mode-1 inverse
// warp. Inputs and outputs are compact landmark/transform metadata only.
inline bool InBounds(const std::array<float, 6>& m) noexcept {
    for (const auto v : m)
        if (!std::isfinite(v))
            return false;
    for (const float y : {0.f, 111.f})
        for (const float x : {0.f, 111.f}) {
            const float sx = m[0] * x + m[1] * y + m[2], sy = m[3] * x + m[4] * y + m[5];
            if (!std::isfinite(sx) || !std::isfinite(sy) || sx < -.5f || sy < -.5f || sx >= 1919.5f ||
                sy >= 1079.5f)
                return false;
        }
    return true;
}
inline std::optional<std::array<float, 6>> Affine(const Points& p, bool eyes_center) noexcept {
    for (const auto& q : p)
        if (!std::isfinite(q.x) || !std::isfinite(q.y))
            return {};
    const float left   = std::min({p[0].x, p[1].x, p[3].x, p[4].x});
    const float right  = std::max({p[0].x, p[1].x, p[3].x, p[4].x});
    const float top    = std::min({p[0].y, p[1].y, p[3].y, p[4].y});
    const float bottom = std::max({p[0].y, p[1].y, p[3].y, p[4].y});
    const float width = right - left, height = bottom - top;
    const float actual = std::sqrt((width * width + height * height) / 2.f);
    const float dx = p[1].x - p[0].x, dy = p[1].y - p[0].y, eye = std::hypot(dx, dy);
    if (!(actual > 0.f) || !(eye > 0.f) || !std::isfinite(actual) || !std::isfinite(eye))
        return {};
    const float scale = actual / (112.f * 0.4f);
    const float a = scale * dx / eye, b = -scale * dy / eye, c = -b, d = a;
    const float cx = eyes_center ? (p[0].x + p[1].x) / 2.f : (p[0].x + p[1].x + p[3].x + p[4].x) / 4.f;
    const float cy = eyes_center ? (p[0].y + p[1].y) / 2.f : (p[0].y + p[1].y + p[3].y + p[4].y) / 4.f;
    const std::array<float, 6> m{a, b, cx - 56.f * (a + b), c, d, cy - 56.f * (c + d)};
    if (!InBounds(m))
        return {};
    return m;
}
}  // namespace cosmo::face_landmark_candidate
