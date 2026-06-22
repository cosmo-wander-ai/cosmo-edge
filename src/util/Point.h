#pragma once

#include <cmath>

namespace cosmo::util {

template <typename T>
class Point_ {
public:
    using value_type = T;

    //! default constructor
    Point_() : x(0), y(0) {}
    Point_(T _x, T _y) : x(_x), y(_y) {}
    Point_(const Point_&)            = default;
    Point_& operator=(const Point_&) = default;

    Point_ operator+(const Point_& p) const {
        return {static_cast<T>(x + p.x), static_cast<T>(y + p.y)};
    }
    Point_ operator-(const Point_& p) const {
        return {static_cast<T>(x - p.x), static_cast<T>(y - p.y)};
    }

    template <typename U>
    Point_ operator*(U t) const {
        return {static_cast<T>(x * t), static_cast<T>(y * t)};
    }

    template <typename U>
    Point_ operator/(U t) const {
        if (t == U{0})
            return {T{0}, T{0}};
        return {static_cast<T>(x / t), static_cast<T>(y / t)};
    }

    T x;
    T y;
};

using Point2i = Point_<int>;
using Point2f = Point_<float>;
using Point2d = Point_<double>;
using Point   = Point2i;

/// Returns the perpendicular vector (90° counter-clockwise rotation).
template <typename PointType>
[[nodiscard]] PointType Perpendicular(const PointType& p) {
    return {-p.y, p.x};
}

/// Returns the squared length of a 2D vector.
template <typename PointType>
[[nodiscard]] auto LengthSquare(const PointType& p) {
    return p.x * p.x + p.y * p.y;
}

/// Returns the Euclidean length of a 2D vector.
template <typename PointType>
[[nodiscard]] auto Length(const PointType& p) {
    return std::sqrt(LengthSquare(p));
}

/// Scales a direction vector to the given length.
/// Returns a zero vector when the input vector has near-zero length.
template <typename PointType, typename LengthT>
[[nodiscard]] PointType LineDirection(const PointType& p, LengthT length) {
    auto len                = Length(p);
    constexpr auto kEpsilon = decltype(len){1e-6};
    if (len < kEpsilon) {
        return {0, 0};
    }
    return p * (length / len);
}

}  // namespace cosmo::util
