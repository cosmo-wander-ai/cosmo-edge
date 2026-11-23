#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cosmo::native_face {
// Match the video ActionService's accepted model-code aliases. If any alias
// requests the strict code, require a single unambiguous binding matching the
// detector's resolved code. This does not authorize a model or enable native
// execution; that remains the job of the deployment and witness checks.
inline bool IsModelCodeKey(std::string_view key) {
    constexpr std::string_view whitespace = " \t\f\v\r\n";
    const auto first                      = key.find_first_not_of(whitespace);
    if (first == key.npos)
        return false;
    key                               = key.substr(first, key.find_last_not_of(whitespace) - first + 1);
    constexpr std::string_view suffix = ".atomicCode";
    return key == "atomicCode" || key == "atomCode" || key == "atomic_code" || key == "atomiccode" ||
           key == "modelCode" || key == "algorithm_code" ||
           (key.size() >= suffix.size() && key.substr(key.size() - suffix.size()) == suffix);
}

inline bool ConsistentStrictCode(const std::string& required, const std::string& resolved,
                                 const std::vector<std::pair<std::string, std::string>>& params) {
    if (required.empty())
        return true;
    std::size_t count = 0;
    bool mentioned    = resolved == required;
    std::string value;
    for (const auto& param : params) {
        if (!IsModelCodeKey(param.first))
            continue;
        ++count;
        value     = param.second;
        mentioned = mentioned || value == required;
    }
    return !mentioned || (count == 1 && value == required && resolved == required);
}
}  // namespace cosmo::native_face
