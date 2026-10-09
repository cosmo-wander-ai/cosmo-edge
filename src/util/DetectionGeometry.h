#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "util/Point.h"

namespace cosmo::util {

// Consecutive vertices in image coordinates. Geometry is not clipped to the
// image: clipping belongs to the consumer (drawing, cropping or ROI checks).
using Quad = std::array<Point2f, 4>;

struct FloatBounds {
    float x1;
    float y1;
    float x2;
    float y2;
};

inline Quad MakeOrientedQuad(float cx, float cy, float width, float height, float radians) {
    const float dx = std::cos(radians) * width / 2.f;
    const float dy = std::sin(radians) * width / 2.f;
    const float hx = -std::sin(radians) * height / 2.f;
    const float hy = std::cos(radians) * height / 2.f;
    return {{{cx - dx - hx, cy - dy - hy},
             {cx + dx - hx, cy + dy - hy},
             {cx + dx + hx, cy + dy + hy},
             {cx - dx + hx, cy - dy + hy}}};
}

inline FloatBounds QuadBounds(const Quad& quad) {
    FloatBounds bounds{quad[0].x, quad[0].y, quad[0].x, quad[0].y};
    for (const auto& point : quad) {
        bounds.x1 = std::min(bounds.x1, point.x);
        bounds.y1 = std::min(bounds.y1, point.y);
        bounds.x2 = std::max(bounds.x2, point.x);
        bounds.y2 = std::max(bounds.y2, point.y);
    }
    return bounds;
}

}  // namespace cosmo::util
