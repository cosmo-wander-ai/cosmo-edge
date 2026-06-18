// JSON compatibility layer — unified type aliases and helper functions.
// All project code should include this header (or nlohmann/json_fwd.hpp) instead
// of including nlohmann/json.hpp directly in header files.

#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace cosmo::util {

/// Canonical JSON type alias for the entire project.
using Json = nlohmann::json;

/// Parse a JSON string. Returns a default-constructed Json on failure.
[[nodiscard]] inline Json ParseJson(const std::string& input) {
    try {
        return Json::parse(input);
    } catch (const Json::parse_error&) {
        return Json{};
    }
}

/// Serialize a Json value to a string.
/// @param indent  Indentation depth (negative = compact, 0+ = pretty-print).
[[nodiscard]] inline std::string SerializeJson(const Json& j, int indent = -1) {
    return j.dump(indent);
}

/// Deserialize a JSON string into a C++ struct.
/// Requires that `from_json(const nlohmann::json&, T&)` is defined for T.
template <typename T>
[[nodiscard]] bool FromJsonString(const std::string& input, T& out) {
    try {
        auto j = Json::parse(input);
        j.get_to(out);
        return true;
    } catch (const Json::exception&) {
        return false;
    }
}

/// Serialize a C++ struct to a JSON string.
/// Requires that `to_json(nlohmann::json&, const T&)` is defined for T.
template <typename T>
[[nodiscard]] std::string ToJsonString(const T& value, int indent = -1) {
    try {
        Json j = value;
        return j.dump(indent);
    } catch (const Json::exception&) {
        return "{}";
    }
}

}  // namespace cosmo::util
