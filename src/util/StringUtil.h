#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cosmo::util {

// Type conversion for types defining implicit string conversion
template <typename T, typename = std::void_t<>>
struct ToStringHelper {
    template <typename U>
    std::string operator()(U&& t) {
        return std::forward<U>(t);
    }
};

// Type conversion for types defining std::to_string
template <typename T>
struct ToStringHelper<T, std::void_t<decltype(std::to_string(std::declval<T>()))>> {
    template <typename U>
    std::string operator()(U&& t) {
        return std::to_string(std::forward<U>(t));
    }
};

template <typename T>
std::string ToString(T&& t) {
    return ToStringHelper<T>()(std::forward<T>(t));
}

// Convert to uppercase
std::string ToUpper(std::string_view str);
// Convert to lowercase
std::string ToLower(std::string_view str);

// Split string by separator
std::vector<std::string_view> Split(std::string_view sv, std::string_view delimiters);
// Trim leading and trailing whitespace characters
std::string_view Trim(std::string_view sv, std::string_view ch = " \t\f\v\r\n");
// Calculate the number of UTF-8 characters
size_t UTF8Length(std::string_view str);

template <typename T>
std::string VectorToString(const std::vector<T>& val) {
    if (val.empty())
        return "";

    std::stringstream stream;
    stream << "[";
    for (size_t i = 0; i < val.size(); ++i) {
        stream << val[i];
        if (i != val.size() - 1)
            stream << ",";
    }
    stream << "]";
    return stream.str();
}

// Join a vector of strings with a separator (default: ",").
std::string JoinStrings(const std::vector<std::string>& items, const char* sep = ",");

// Extract the last path segment from a URI / file path.
// e.g. "/api/v1/devices" → "devices"
std::string_view GetLastPathSegment(std::string_view uri);

}  // namespace cosmo::util
