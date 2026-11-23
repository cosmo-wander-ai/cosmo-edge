#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace cosmo::native_face {

// A startup selector requests strict handling; it never authorizes a model.
// The legacy single-code setting can coexist with the new comma-separated set.
// Invalid configuration is represented separately from an empty selection.
struct RequiredCodes {
    bool valid{true};
    std::vector<std::string> codes;
};

template <typename IsSafe>
RequiredCodes ParseRequiredCodes(const std::string& legacy, const std::string& multiple, IsSafe is_safe) {
    RequiredCodes result;
    auto add = [&](const std::string& code, bool allow_existing) {
        if (code.empty() || !is_safe(code))
            return false;
        const auto found = std::find(result.codes.begin(), result.codes.end(), code);
        if (found != result.codes.end())
            return allow_existing;
        if (result.codes.size() >= 32)
            return false;
        result.codes.push_back(code);
        return true;
    };
    if (!legacy.empty() && !add(legacy, false))
        return {false, {}};
    if (!multiple.empty()) {
        std::vector<std::string> seen_in_list;
        std::size_t begin = 0;
        for (;;) {
            const auto end  = multiple.find(',', begin);
            const auto code = multiple.substr(begin, end == std::string::npos ? end : end - begin);
            if (std::find(seen_in_list.begin(), seen_in_list.end(), code) != seen_in_list.end() ||
                !add(code, true))
                return {false, {}};
            seen_in_list.push_back(code);
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
    }
    return result;
}

}  // namespace cosmo::native_face
