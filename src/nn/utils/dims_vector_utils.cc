#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

int DimsVectorUtils::Count(const DimsVector& dims, int start, int end) {
    if (end == -1 || end > dims.size()) {
        end = static_cast<int>(dims.size());
    }

    int result = 1;
    for (int index = start; index < end; ++index) {
        result *= dims.at(index);
    }

    return result;
}

bool DimsVectorUtils::Equal(const DimsVector& dims0, const DimsVector& dims1, int start, int end) {
    if (dims0.size() == 0 && dims1.size() == 0)
        return true;

    if (dims0.size() < start)
        return false;

    if (end == -1 || end > dims0.size()) {
        end = static_cast<int>(dims0.size());
    }

    if (dims0.size() != dims1.size())
        return false;

    for (int i = start; i < end; i++) {
        if (dims0.at(i) != dims1.at(i))
            return false;
    }

    return true;
}

DimsVector DimsVectorUtils::NCHW2NHWC(const DimsVector& dims) {
    ASSERT(dims.size() == 4);
    const int n           = dims.at(0);
    const int c           = dims.at(1);
    const int h           = dims.at(2);
    const int w           = dims.at(3);
    std::vector<int> nhwc = {n, h, w, c};
    return nhwc;
}

DimsVector DimsVectorUtils::NHWC2NCHW(const DimsVector& dims) {
    ASSERT(dims.size() == 4);
    const int n           = dims.at(0);
    const int h           = dims.at(1);
    const int w           = dims.at(2);
    const int c           = dims.at(3);
    std::vector<int> nchw = {n, c, h, w};
    return nchw;
}

}  // namespace cosmo::nn