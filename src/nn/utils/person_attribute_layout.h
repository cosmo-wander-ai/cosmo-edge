#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cosmo::nn {

inline constexpr int kPersonAttributeDetectionColumns = 6;

inline bool IsValidPersonAttributeName(std::string_view name) {
    const auto is_alnum = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    return !name.empty() && name != "person" && is_alnum(name.front()) &&
           std::all_of(name.begin() + 1, name.end(),
                       [&](char c) { return is_alnum(c) || c == '_' || c == '-'; });
}

inline bool IsValidPersonAttributeNames(int row_width, const std::vector<std::string>& names) {
    if (row_width <= kPersonAttributeDetectionColumns ||
        names.size() != static_cast<size_t>(row_width - kPersonAttributeDetectionColumns))
        return false;
    std::unordered_set<std::string_view> unique;
    return std::all_of(names.begin(), names.end(), [&](const auto& name) {
        return IsValidPersonAttributeName(name) && unique.insert(name).second;
    });
}

// The manifest orders the scores after xyxy, person score and class ID. A missing
// manifest field supports the known legacy layouts. An explicit field is authoritative.
inline std::optional<std::vector<std::string>> ResolvePersonAttributeNames(
    int row_width, const nlohmann::json* configured_names = nullptr) {
    if (!configured_names) {
        switch (row_width) {
            case kPersonAttributeDetectionColumns + 4:
                return std::vector<std::string>{"smoking", "sleep", "fall", "normal"};
            case kPersonAttributeDetectionColumns + 7:
                return std::vector<std::string>{"smoking",   "sleep",    "fall",  "phone_call",
                                                "phone_use", "climbing", "normal"};
            default:
                return std::nullopt;
        }
    }
    if (row_width <= kPersonAttributeDetectionColumns || !configured_names->is_array() ||
        configured_names->size() != static_cast<size_t>(row_width - kPersonAttributeDetectionColumns))
        return std::nullopt;
    std::vector<std::string> names;
    names.reserve(configured_names->size());
    for (const auto& entry : *configured_names) {
        if (!entry.is_string())
            return std::nullopt;
        names.push_back(entry.get<std::string>());
    }
    if (!IsValidPersonAttributeNames(row_width, names))
        return std::nullopt;
    return names;
}

}  // namespace cosmo::nn
