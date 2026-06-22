/// @file EnvUtil.h
/// @brief Environment variable utilities — shared across modules.
#pragma once

#include <cstdlib>
#include <string>

namespace cosmo::util {

/// Read an environment variable, returning @p default_value if unset or empty.
inline std::string GetEnvOrDefault(const char* key, const std::string& default_value) {
    const char* value = std::getenv(key);
    if (!value || value[0] == '\0') {
        return default_value;
    }
    return value;
}

/// Read an environment variable as int, returning @p default_value if unset, empty, or invalid.
/// Valid range: 1–65535.
inline int GetEnvIntOrDefault(const char* key, int default_value) {
    const char* value = std::getenv(key);
    if (!value || value[0] == '\0') {
        return default_value;
    }
    char* end   = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || parsed <= 0 || parsed > 65535) {
        return default_value;
    }
    return static_cast<int>(parsed);
}

}  // namespace cosmo::util
