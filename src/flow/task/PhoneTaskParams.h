#pragma once
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "util/dto/TaskAreaTypes.h"
namespace cosmo::phone_candidate {
inline std::optional<float> PhoneThreshold(const MsgTaskConfig& config, const std::string& label) {
    const std::string key = "aiParam." + label + ".confidence";
    std::optional<float> out;
    for (const auto& kv : config.params) {
        const auto& direct = kv.key.ToRefString();
        if (!direct.empty() && !kv.keys.empty()) {
            std::string joined;
            for (const auto& part : kv.keys) {
                if (!joined.empty())
                    joined += '.';
                joined += part;
            }
            if (joined != direct)
                return {};
        }
        const bool structured = kv.keys.size() == 3 && kv.keys[0] == "aiParam" && kv.keys[1] == label &&
                                kv.keys[2] == "confidence";
        if (!direct.empty()) {
            if (direct != key)
                continue;
        } else if (!structured)
            continue;
        if (out)
            return {};
        const auto& text  = kv.value.ToRefString();
        char* end         = nullptr;
        errno             = 0;
        const float value = std::strtof(text.c_str(), &end);
        if (errno || end == text.c_str() || *end != '\0' || !std::isfinite(value) || value < 0.f ||
            value > 1.f)
            return {};
        out = value;
    }
    return out;
}
}  // namespace cosmo::phone_candidate
