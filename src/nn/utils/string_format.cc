#include "nn/utils/string_format.h"

#include <iomanip>

namespace cosmo::nn {

std::string DoubleToString(double val) {
    std::stringstream stream;
    stream << std::setprecision(4) << std::setiosflags(std::ios::fixed) << val;
    return stream.str();
}

std::string FloatToString(float val) {
    std::stringstream stream;
    stream << std::setprecision(2) << std::setiosflags(std::ios::fixed) << val;
    return stream.str();
}

std::string BoolToString(bool val) {
    std::stringstream stream;
    stream << std::boolalpha << val;
    return stream.str();
}

std::string DeviceTypeToString(DeviceType val) {
    switch (val) {
        case DEVICE_NAIVE:
            return "NAIVE";
        case DEVICE_SOPHON_TPU:
            return "SOPHON_TPU";
        default:
            return "UNKNOWN";
    }
}

template <>
std::string VectorToString(std::vector<int8_t> val) {
    if (val.empty())
        return "";
    std::stringstream stream;
    stream << "[";
    for (int i = 0; i < val.size(); ++i) {
        int8_t val_ = (int8_t)val[i];
        int v       = val_ & 0xff;
        stream << v;
        if (i != val.size() - 1)
            stream << ",";
    }
    stream << "]";
    return stream.str();
}

}  // namespace cosmo::nn