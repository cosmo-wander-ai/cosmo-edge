#pragma once

#include <algorithm>
#include <cmath>

#include "nn/core/macros.h"
#include "nn/utils/point.h"

namespace cosmo::nn {

template <typename _Tp>
class PUBLIC Rect_ {
public:
    typedef _Tp value_type;

    //! default constructor
    Rect_();
    Rect_(_Tp _x, _Tp _y, _Tp _width, _Tp _height);
    Rect_(const Rect_& r);
    Rect_(const Point_<_Tp>& pt1, const Point_<_Tp>& pt2);

    Rect_& operator=(const Rect_& r);
    //! the top-left corner
    Point_<_Tp> tl() const;
    //! the bottom-right corner
    Point_<_Tp> br() const;

    //! true if empty
    bool empty() const;

    //! checks whether the rectangle contains the point
    bool contains(const Point_<_Tp>& pt) const;

    //! area (width*height) of the rectangle
    _Tp area() const;

    _Tp x;       //!< x coordinate of the top-left corner
    _Tp y;       //!< y coordinate of the top-left corner
    _Tp width;   //!< width of the rectangle
    _Tp height;  //!< height of the rectangle
};

typedef Rect_<int> Rect2i;
typedef Rect_<float> Rect2f;
typedef Rect_<double> Rect2d;
typedef Rect2i Rect;

template <typename _Tp>
inline Rect_<_Tp>::Rect_() : x(0), y(0), width(0), height(0) {}

template <typename _Tp>
inline Rect_<_Tp>::Rect_(_Tp _x, _Tp _y, _Tp _width, _Tp _height)
    : x(_x), y(_y), width(_width), height(_height) {}

template <typename _Tp>
inline Rect_<_Tp>::Rect_(const Rect_<_Tp>& r) : x(r.x), y(r.y), width(r.width), height(r.height) {}

template <typename _Tp>
inline Rect_<_Tp>::Rect_(const Point_<_Tp>& pt1, const Point_<_Tp>& pt2) {
    x      = std::min(pt1.x, pt2.x);
    y      = std::min(pt1.y, pt2.y);
    width  = std::max(pt1.x, pt2.x) - x;
    height = std::max(pt1.y, pt2.y) - y;
}

template <typename _Tp>
inline Rect_<_Tp>& Rect_<_Tp>::operator=(const Rect_<_Tp>& r) {
    x      = r.x;
    y      = r.y;
    width  = r.width;
    height = r.height;
    return *this;
}

template <typename _Tp>
inline Point_<_Tp> Rect_<_Tp>::tl() const {
    return Point_<_Tp>(x, y);
}

template <typename _Tp>
inline Point_<_Tp> Rect_<_Tp>::br() const {
    return Point_<_Tp>(x + width, y + height);
}

template <typename _Tp>
inline bool Rect_<_Tp>::empty() const {
    return width <= 0 || height <= 0;
}

template <typename _Tp>
inline bool Rect_<_Tp>::contains(const Point_<_Tp>& pt) const {
    return x <= pt.x && pt.x < x + width && y <= pt.y && pt.y < y + height;
}

template <typename _Tp>
inline _Tp Rect_<_Tp>::area() const {
    const _Tp result = width * height;
    return result;
}

template <typename _Tp>
static inline Rect_<_Tp>& operator+=(Rect_<_Tp>& a, const Point_<_Tp>& b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

template <typename _Tp>
static inline Rect_<_Tp>& operator-=(Rect_<_Tp>& a, const Point_<_Tp>& b) {
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

template <typename _Tp>
static inline Rect_<_Tp>& operator&=(Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    _Tp x1   = std::max(a.x, b.x);
    _Tp y1   = std::max(a.y, b.y);
    a.width  = std::min(a.x + a.width, b.x + b.width) - x1;
    a.height = std::min(a.y + a.height, b.y + b.height) - y1;
    a.x      = x1;
    a.y      = y1;
    if (a.width <= 0 || a.height <= 0)
        a = Rect_<_Tp>();
    return a;
}

template <typename _Tp>
static inline Rect_<_Tp>& operator|=(Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    if (a.empty()) {
        a = b;
    } else if (!b.empty()) {
        _Tp x1   = std::min(a.x, b.x);
        _Tp y1   = std::min(a.y, b.y);
        a.width  = std::max(a.x + a.width, b.x + b.width) - x1;
        a.height = std::max(a.y + a.height, b.y + b.height) - y1;
        a.x      = x1;
        a.y      = y1;
    }
    return a;
}

template <typename _Tp>
static inline bool operator==(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

template <typename _Tp>
static inline bool operator!=(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    return a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height;
}

template <typename _Tp>
static inline Rect_<_Tp> operator+(const Rect_<_Tp>& a, const Point_<_Tp>& b) {
    return Rect_<_Tp>(a.x + b.x, a.y + b.y, a.width, a.height);
}

template <typename _Tp>
static inline Rect_<_Tp> operator-(const Rect_<_Tp>& a, const Point_<_Tp>& b) {
    return Rect_<_Tp>(a.x - b.x, a.y - b.y, a.width, a.height);
}

template <typename _Tp>
static inline Rect_<_Tp> operator&(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    Rect_<_Tp> c = a;
    return c &= b;
}

template <typename _Tp>
static inline Rect_<_Tp> operator|(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    Rect_<_Tp> c = a;
    return c |= b;
}

template <typename _Tp>
static inline double GetIOU(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    _Tp in = (a & b).area();
    _Tp un = a.area() + b.area() - in;
    if (un < __DBL_EPSILON__)
        return 0;

    return double(in) / un;
}

template <typename _Tp>
static inline double GetDIOU(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    double iou = 0;
    double in  = (a & b).area();
    double un  = a.area() + b.area() - in;
    iou        = un < __DBL_EPSILON__ ? 0 : in / un;

    Rect_<_Tp> convex = a | b;
    double d = Distance(a, b) / std::sqrt(convex.height * convex.height + convex.width * convex.width);
    return iou - d;
}

template <typename _Tp>
static inline double Distance(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    double dx = (a.x + a.width / 2) - (b.x + b.width / 2);
    double dy = (a.y + a.height / 2) - (b.y + b.height / 2);

    return std::sqrt(dx * dx + dy * dy);
}

template <typename _Tp>
static inline bool Contains(const Rect_<_Tp>& a, const Rect_<_Tp>& b) {
    if (a.empty())
        return false;

    return a.contains(b.tl()) && a.contains(b.br());
}

}  // namespace cosmo::nn
