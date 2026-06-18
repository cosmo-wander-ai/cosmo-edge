#pragma once

#include "nn/core/common.h"
#include "nn/core/macros.h"
#include "nn/core/status.h"

namespace cosmo::nn {
class PUBLIC DimsVectorUtils {
public:
    // [start end)
    static int Count(const DimsVector& dims, int start = 0, int end = -1);

    static bool Equal(const DimsVector& dims0, const DimsVector& dims1, int start = 0, int end = -1);

    static DimsVector NCHW2NHWC(const DimsVector& dims);

    static DimsVector NHWC2NCHW(const DimsVector& dims);
};
}  // namespace cosmo::nn
