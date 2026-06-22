// Geometric calculation primitives — point/line/polygon relations.
// All functions are header-only templates, safe for cross-type usage
// (int, float, double point types).

#pragma once

#include <cmath>

namespace cosmo::util {

/// Compare two floating-point values; returns true when they differ
/// by more than a fixed epsilon (1e-5).
constexpr bool FloatDiff(double a, double b) {
    // Cannot use std::fabs in constexpr before C++23, but the ternary works.
    return ((a - b) >= 0 ? (a - b) : (b - a)) >= 0.00001;
}

// Position of point P relative to directed line A→B.
// Returns negative when P is on the left side (screen-coords: below),
// positive on the right side, zero when P is exactly on the line.
template <typename Point>
decltype(auto) PointRelativeLine(const Point& P, const Point& A, const Point& B) {
    return (P.x - A.x) * (B.y - A.y) - (P.y - A.y) * (B.x - A.x);
}

// Euclidean distance between two points.
template <typename Point>
decltype(auto) PointDistance(const Point& A, const Point& B) {
    return std::sqrt((A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y));
}

// Shortest distance from point P to line segment A-B (non-negative).
template <typename Point>
decltype(auto) PointLineDistance(const Point& P, const Point& A, const Point& B) {
    if ((P.x - A.x) * (B.x - A.x) + (P.y - A.y) * (B.y - A.y) <= 0) {
        return PointDistance(P, A);
    } else if ((P.x - B.x) * (A.x - B.x) + (P.y - B.y) * (A.y - B.y) <= 0) {
        return PointDistance(P, B);
    }
    return std::abs(PointRelativeLine(P, A, B)) / PointDistance(A, B);
}

// Check whether two values have the same sign.
// Returns 1 if same, -1 if different, 0 if either is zero.
template <typename T>
int JudgeSign(T a, T b) {
    int x = a < 0 ? -1 : (a > 0 ? 1 : 0);
    int y = b < 0 ? -1 : (b > 0 ? 1 : 0);
    return x * y;
}

// Position of point P relative to a polygon defined by Ps[0..size-1].
// Returns -1 if inside, 0 if on an edge, 1 if outside.
template <typename Point>
int PointRelativePolygon(const Point& P, const Point* Ps, size_t size) {
    if (size == 0) {
        return 0;
    }
    int c = 1;
    for (size_t i = 0, j = size - 1; i < size; j = i++) {
        // Check intersection with the horizontal ray from P.
        if ((Ps[i].y < P.y) != (Ps[j].y < P.y)) {
            auto x1 = (Ps[j].x - Ps[i].x) * (P.y - Ps[i].y) / (Ps[j].y - Ps[i].y) + Ps[i].x;
            // Point is on the edge.
            if (P.x == x1) {
                return 0;
            }
            // Point is to the left of the edge.
            if (P.x < x1) {
                c = -c;
            }
        }
        // Point is on a horizontal edge.
        else if (Ps[i].y == P.y && Ps[j].y == P.y && (Ps[i].x < P.x) != (Ps[j].x < P.x)) {
            return 0;
        }
    }
    return c;
}

// Segment intersection test for segments A1-A2 and B1-B2.
// Returns -1 if crossing, 0 if an endpoint lies on the other segment, 1 if no intersection.
// A1/A2 and B1/B2 must not be identical points.
template <typename Point>
int LineIntersection(const Point& A1, const Point& A2, const Point& B1, const Point& B2) {
    // Check whether A1, A2 are on opposite sides of line B.
    auto signA = JudgeSign(PointRelativeLine(A1, B1, B2), PointRelativeLine(A2, B1, B2));
    // Check whether B1, B2 are on opposite sides of line A.
    auto signB = JudgeSign(PointRelativeLine(B1, A1, A2), PointRelativeLine(B2, A1, A2));
    auto sign  = signA + signB;
    if (sign <= -2) {
        return -1;
    } else if (0 == signA * signB) {
        return 0;
    }
    return 1;
}

// Polygon intersection test. Returns true if Ps1 and Ps2 intersect or overlap.
template <typename Point>
bool PolygonIntersection(const Point* Ps1, int size1, const Point* Ps2, int size2) {
    // Check edge-edge intersections.
    for (int i = 0; i < size1; ++i) {
        for (int j = 0; j < size2; ++j) {
            Point A1 = Ps1[i == 0 ? size1 - 1 : i - 1];
            Point A2 = Ps1[i];
            Point B1 = Ps2[j == 0 ? size2 - 1 : j - 1];
            Point B2 = Ps2[j];

            if (LineIntersection(A1, A2, B1, B2) <= 0) {
                return true;
            }
        }
    }

    // Check containment (one polygon entirely inside the other).
    {
        for (int i = 0; i < size1; ++i) {
            if (PointRelativePolygon(Ps1[i], Ps2, static_cast<size_t>(size2)) <= 0) {
                return true;
            }
        }
        for (int i = 0; i < size2; ++i) {
            if (PointRelativePolygon(Ps2[i], Ps1, static_cast<size_t>(size1)) <= 0) {
                return true;
            }
        }
    }

    return false;
}

}  // namespace cosmo::util

// Legacy macro — prefer cosmo::util::FloatDiff() in new code.
#define MV_FLOAT_VALUE_DIFF(value1, value2) (cosmo::util::FloatDiff((value1), (value2)))
