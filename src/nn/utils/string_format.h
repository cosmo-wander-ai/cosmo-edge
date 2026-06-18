#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "nn/core/common.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

std::string DoubleToString(double val);

std::string FloatToString(float val);

std::string BoolToString(bool val);

std::string DeviceTypeToString(DeviceType val);

template <typename T>
std::string VectorToString(std::vector<T> val) {
    if (val.empty())
        return "";

    std::stringstream stream;
    stream << "[";
    for (int i = 0; i < val.size(); ++i) {
        stream << val[i];
        if (i != val.size() - 1)
            stream << ",";
    }
    stream << "]";
    return stream.str();
}

template <>
std::string VectorToString(std::vector<int8_t> val);

template <typename T>
std::vector<T> Spilt(const std::string& src, const std::string& sep) {
    std::string::size_type pos;

    std::string str = src;
    str.append(sep);

    size_t size = str.size();
    std::stringstream ss;
    std::vector<T> result;
    for (size_t i = 0; i < size; i++) {
        pos = str.find(sep, i);

        if (pos < size) {
            ss.clear();

            std::string s = str.substr(i, pos - i);
            ss << s;

            T t;
            ss >> t;

            result.push_back(t);
            i = pos + sep.size() - 1;
        }
    }

    return result;
}

template <typename T>
std::string Join(const std::vector<T>& vec, const std::string& sep) {
    std::stringstream ss;
    for (size_t i = 0; i < vec.size(); ++i) {
        ss << vec[i];
        if (i != vec.size() - 1)
            ss << sep;
    }
    return ss.str();
}

}  // namespace cosmo::nn
