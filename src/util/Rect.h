#pragma once

#include <algorithm>

#include "util/Point.h"

namespace cosmo::util {

template <typename T>
class Box_ {
public:
    using value_type = T;

    //! default constructor
    Box_() : x(0), y(0), width(0), height(0) {}
    Box_(T _x, T _y, T _width, T _height) : x(_x), y(_y), width(_width), height(_height) {}
    Box_(const Box_& r) : x(r.x), y(r.y), width(r.width), height(r.height) {}
    Box_(const Point_<T>& pt1, const Point_<T>& pt2)
        : x(std::min(pt1.x, pt2.x)),
          y(std::min(pt1.y, pt2.y)),
          width(std::max(pt1.x, pt2.x) - x),
          height(std::max(pt1.y, pt2.y) - y) {}

    Box_& operator=(const Box_& r) {
        x      = r.x;
        y      = r.y;
        width  = r.width;
        height = r.height;
        return *this;
    }

    //! the top-left corner
    Point_<T> tl() const {
        return Point_<T>(x, y);
    }

    //! the bottom-right corner
    Point_<T> br() const {
        return Point_<T>(x + width, y + height);
    }

    //! true if empty
    bool empty() const {
        return width <= 0 || height <= 0;
    }

    //! checks whether the rectangle contains the point
    bool contains(const Point_<T>& pt) const {
        return x <= pt.x && pt.x < x + width && y <= pt.y && pt.y < y + height;
    }

    //! area (width*height) of the rectangle
    T area() const {
        return width * height;
    }

    T x;       //!< x coordinate of the top-left corner
    T y;       //!< y coordinate of the top-left corner
    T width;   //!< width of the rectangle
    T height;  //!< height of the rectangle
};

using Box2i = Box_<int>;
using Box2f = Box_<float>;
using Box2d = Box_<double>;
using Box   = Box2i;

template <typename T>
static inline Box_<T>& operator+=(Box_<T>& a, const Point_<T>& b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

template <typename T>
static inline Box_<T>& operator-=(Box_<T>& a, const Point_<T>& b) {
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

template <typename T>
static inline Box_<T>& operator&=(Box_<T>& a, const Box_<T>& b) {
    T x1     = std::max(a.x, b.x);
    T y1     = std::max(a.y, b.y);
    a.width  = std::min(a.x + a.width, b.x + b.width) - x1;
    a.height = std::min(a.y + a.height, b.y + b.height) - y1;
    a.x      = x1;
    a.y      = y1;
    if (a.width <= 0 || a.height <= 0)
        a = Box_<T>();
    return a;
}

template <typename T>
static inline Box_<T>& operator|=(Box_<T>& a, const Box_<T>& b) {
    if (a.empty()) {
        a = b;
    } else if (!b.empty()) {
        T x1     = std::min(a.x, b.x);
        T y1     = std::min(a.y, b.y);
        a.width  = std::max(a.x + a.width, b.x + b.width) - x1;
        a.height = std::max(a.y + a.height, b.y + b.height) - y1;
        a.x      = x1;
        a.y      = y1;
    }
    return a;
}

template <typename T>
static inline bool operator==(const Box_<T>& a, const Box_<T>& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

template <typename T>
static inline bool operator!=(const Box_<T>& a, const Box_<T>& b) {
    return a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height;
}

template <typename T>
static inline Box_<T> operator+(const Box_<T>& a, const Point_<T>& b) {
    return Box_<T>(a.x + b.x, a.y + b.y, a.width, a.height);
}

template <typename T>
static inline Box_<T> operator-(const Box_<T>& a, const Point_<T>& b) {
    return Box_<T>(a.x - b.x, a.y - b.y, a.width, a.height);
}

template <typename T>
static inline Box_<T> operator&(const Box_<T>& a, const Box_<T>& b) {
    Box_<T> c = a;
    return c &= b;
}

template <typename T>
static inline Box_<T> operator|(const Box_<T>& a, const Box_<T>& b) {
    Box_<T> c = a;
    return c |= b;
}

}  // namespace cosmo::util
