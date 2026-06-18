// A utility for types with constrained ranges or lengths.
// Provides string and numeric types that validate their values upon construction or assignment.

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "fmt/format.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/StringUtil.h"

namespace cosmo::util {

template <size_t MinSize, size_t MaxSize>
class String {
public:
    String() = default;
    // cppcheck-suppress noExplicitConstructor
    String(std::string&& str) : str_(std::move(str)) {
        Check(str_);
    }
    // cppcheck-suppress noExplicitConstructor
    String(const std::string& str) : str_(str) {
        Check(str_);
    }

    String& operator=(const std::string& str) {
        Check(str);
        str_ = str;
        return *this;
    }

    String& operator=(std::string&& str) {
        Check(str);
        str_ = std::move(str);
        return *this;
    }

    [[nodiscard]] bool empty() const {
        return str_.empty();
    }

    [[nodiscard]] const char* c_str() const {
        return str_.c_str();
    }

    operator std::string const&() const& {
        return str_;
    }

    operator std::string() && {
        return std::move(str_);
    }

    [[nodiscard]] std::string ToString() const {
        return str_;
    }

    [[nodiscard]] const std::string& ToRefString() const {
        return str_;
    }

private:
    void Check(const std::string& str) {
        auto size = cosmo::util::UTF8Length(str);
        if (size > MaxSize || size < MinSize) {
            std::string err_msg =
                fmt::format("String '{}' length {} out of range [{}, {}]", str, size, MinSize, MaxSize);
            throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::ParameterLenError, err_msg.c_str());
        }
    }

    std::string str_;
};

template <size_t Min1, size_t Max1, size_t Min2, size_t Max2>
bool operator==(const String<Min1, Max1>& l, const String<Min2, Max2>& r) {
    return static_cast<std::string>(l) == static_cast<std::string>(r);
}

template <size_t Min1, size_t Max1>
bool operator==(const String<Min1, Max1>& l, const std::string& r) {
    return static_cast<std::string>(l) == r;
}

template <size_t Min1, size_t Max1, size_t Min2, size_t Max2>
bool operator!=(const String<Min1, Max1>& l, const String<Min2, Max2>& r) {
    return static_cast<std::string>(l) != static_cast<std::string>(r);
}

template <size_t Min1, size_t Max1>
bool operator!=(const String<Min1, Max1>& l, const std::string& r) {
    return static_cast<std::string>(l) != r;
}

template <int MinValue, int MaxValue>
class RangeInt {
public:
    RangeInt() = default;
    // cppcheck-suppress noExplicitConstructor
    RangeInt(int value) : value_(value) {
        Check(value_);
    }
    RangeInt& operator=(int value) {
        Check(value);
        value_ = value;
        return *this;
    }

    operator int() const {
        return value_;
    }

    template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
    operator T() const {
        return static_cast<T>(value_);
    }

private:
    void Check(int value) {
        if (value > MaxValue || value < MinValue) {
            std::string err_string =
                fmt::format("Integer {} out of range [{}, {}]", value, MinValue, MaxValue);
            throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::ParameterException, err_string.c_str());
        }
    }

    int value_{0};
};

template <int Min1, int Max1, int Min2, int Max2>
bool operator==(const RangeInt<Min1, Max1>& l, const RangeInt<Min2, Max2>& r) {
    return static_cast<int>(l) == static_cast<int>(r);
}

template <int Min1, int Max1>
bool operator==(const RangeInt<Min1, Max1>& l, int r) {
    return static_cast<int>(l) == r;
}

template <typename T>
class RangeValue {
public:
    // cppcheck-suppress noExplicitConstructor
    RangeValue(T value) : value_(value) {
        Check(value_);
    }
    RangeValue(T min, T max, T value) : min_value_(min), max_value_(max), value_(value) {
        Check(value_);
    }
    RangeValue& operator=(T value) {
        Check(value);
        value_ = value;
        return *this;
    }

    operator T() const {
        return value_;
    }

private:
    void Check(T value) {
        if (value > max_value_ || value < min_value_) {
            std::string err_string =
                fmt::format("Value {} out of range [{}, {}]", value, min_value_, max_value_);
            throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::ParameterException, err_string.c_str());
        }
    }

    T min_value_{std::numeric_limits<T>::lowest()};
    T max_value_{std::numeric_limits<T>::max()};
    T value_{0};
};

template <typename T>
T operator+(RangeValue<T> t1, RangeValue<T> t2) {
    return static_cast<T>(t1) + static_cast<T>(t2);
}

template <typename T>
T operator-(RangeValue<T> t1, RangeValue<T> t2) {
    return static_cast<T>(t1) - static_cast<T>(t2);
}

template <typename T, typename U>
T operator*(RangeValue<T> t1, U v) {
    return static_cast<T>(t1) * v;
}

template <typename T>
T min(RangeValue<T> t1, RangeValue<T> t2) {
    return std::min(static_cast<T>(t1), static_cast<T>(t2));
}

template <typename T>
T max(RangeValue<T> t1, RangeValue<T> t2) {
    return std::max(static_cast<T>(t1), static_cast<T>(t2));
}

template <typename T>
T min(RangeValue<T> t1, T t2) {
    return std::min(static_cast<T>(t1), t2);
}

template <typename T>
T max(RangeValue<T> t1, T t2) {
    return std::max(static_cast<T>(t1), t2);
}
}  // namespace cosmo::util

template <size_t MinSize, size_t MaxSize>
struct fmt::formatter<cosmo::util::String<MinSize, MaxSize>> : public formatter<std::string> {
    template <typename FormatContext>
    constexpr auto format(const cosmo::util::String<MinSize, MaxSize>& a, FormatContext& ctx) {
        return formatter<std::string>::format(a.ToRefString(), ctx);
    }
};

template <int MinValue, int MaxValue>
struct fmt::formatter<cosmo::util::RangeInt<MinValue, MaxValue>> : public formatter<int> {
    template <typename FormatContext>
    constexpr auto format(const cosmo::util::RangeInt<MinValue, MaxValue>& a, FormatContext& ctx) {
        return formatter<int>::format(static_cast<int>(a), ctx);
    }
};

template <typename T>
struct fmt::formatter<cosmo::util::RangeValue<T>> : public formatter<T> {
    template <typename FormatContext>
    constexpr auto format(const cosmo::util::RangeValue<T>& a, FormatContext& ctx) {
        return formatter<T>::format(static_cast<T>(a), ctx);
    }
};
