#pragma once

#include "nn/core/macros.h"

namespace cosmo::nn {

template <typename _Tp>
class PUBLIC Point_ {
public:
    typedef _Tp value_type;

    //! default constructor
    Point_();
    Point_(_Tp _x, _Tp _y);
    Point_(const Point_& pt);

    _Tp x;
    _Tp y;
};

typedef Point_<int> Point_2i;
typedef Point_<float> Point_2f;
typedef Point_<double> Point_2d;
typedef Point_2i Point;

template <typename _Tp>
inline Point_<_Tp>::Point_() : x(0), y(0) {}

template <typename _Tp>
inline Point_<_Tp>::Point_(_Tp _x, _Tp _y) : x(_x), y(_y) {}

template <typename _Tp>
inline Point_<_Tp>::Point_(const Point_& pt) : x(pt.x), y(pt.y) {}

}  // namespace cosmo::nn
