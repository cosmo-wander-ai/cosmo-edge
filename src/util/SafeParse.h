// Safe number parsing utility
//
// Replacement for atoi/atof/atoll, uses std::from_chars / strtod.
// Returns defaultValue on failure instead of UB.
#pragma once

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>

namespace cosmo {
namespace util {

    // Safely parse integers (int, long, long long, uint16_t, uint8_t, etc.)
    // Returns defaultValue on failure
    template <typename T = int>
    inline std::enable_if_t<std::is_integral_v<T>, T> ParseInt(std::string_view str, T defaultValue = 0) {
        size_t start = str.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string_view::npos)
            return defaultValue;
        str.remove_prefix(start);

        if (!str.empty() && str.front() == '+') {
            str.remove_prefix(1);
        }

        if (str.empty())
            return defaultValue;

        T result{};
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
        return (ec == std::errc{}) ? result : defaultValue;
    }

    template <typename T = int>
    inline std::enable_if_t<std::is_integral_v<T>, T> ParseInt(const char* str, T defaultValue = 0) {
        if (!str)
            return defaultValue;
        return ParseInt<T>(std::string_view(str), defaultValue);
    }

    template <typename T = int>
    inline std::enable_if_t<std::is_integral_v<T>, T> ParseInt(const std::string& str, T defaultValue = 0) {
        return ParseInt<T>(std::string_view(str), defaultValue);
    }

    // Safely parse floating point numbers (float, double)
    // Uses strtod, returns defaultValue on failure
    inline double ParseDouble(const char* str, double defaultValue = 0.0) {
        if (!str || *str == '\0')
            return defaultValue;

        errno         = 0;
        char* end     = nullptr;
        double result = std::strtod(str, &end);
        if (end == str || errno == ERANGE)
            return defaultValue;
        return result;
    }

    inline double ParseDouble(const std::string& str, double defaultValue = 0.0) {
        return ParseDouble(str.c_str(), defaultValue);
    }

    inline float ParseFloat(const char* str, float defaultValue = 0.0f) {
        if (!str || *str == '\0')
            return defaultValue;

        errno        = 0;
        char* end    = nullptr;
        float result = std::strtof(str, &end);
        if (end == str || errno == ERANGE)
            return defaultValue;
        return result;
    }

    inline float ParseFloat(const std::string& str, float defaultValue = 0.0f) {
        return ParseFloat(str.c_str(), defaultValue);
    }

}  // namespace util
}  // namespace cosmo
