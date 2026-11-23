#pragma once
#include "util/SleepRoutePolicy.h"
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "service/model/dto/ModelInfo.h"
#include "util/dto/TaskAreaTypes.h"
namespace cosmo::sleep_candidate {
inline bool ParseConfidence(const std::string& s, bool allow_empty) noexcept {
    if (s.empty())
        return allow_empty;
    bool digit = false, dot = false;
    for (unsigned char c : s) {
        if (c >= '0' && c <= '9') {
            digit = true;
            continue;
        }
        if (c == '.' && !dot) {
            dot = true;
            continue;
        }
        return false;
    }
    if (!digit)
        return false;
    errno         = 0;
    char* end     = nullptr;
    const float n = std::strtof(s.c_str(), &end);
    return errno != ERANGE && end && *end == '\0' && std::isfinite(n) && n >= 0.f && n <= 1.f;
}
inline bool ParseConfidenceConfig(const std::string& s) noexcept {
    if (s.size() < 3 || (s[0] != '0' && s[0] != '1') || s[1] != ',')
        return false;
    std::size_t i = 2;
    bool neg      = false;
    if (i < s.size() && s[i] == '-') {
        neg = true;
        ++i;
    }
    if (i == s.size())
        return false;
    int v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9')
            return false;
        v = v * 10 + (s[i] - '0');
        if (v > 100)
            return false;
    }
    (void)neg;
    return true;
}
inline bool SleepConfidencePatch(const std::vector<MsgDynamicKeyValue>& params, bool require_all) noexcept {
    try {
        const std::array<std::string, 3> labels{"pedestrian", "pedSleepStomach", "pedSleepLieDown"};
        std::array<int, 3> seen{};
        std::array<int, 3> configs{};
        for (const auto& p : params)
            for (std::size_t i = 0; i < labels.size(); ++i) {
                const auto& k = p.key.ToRefString();
                const auto& v = p.value.ToRefString();
                if (k == "aiParam." + labels[i] + ".confidence") {
                    if (++seen[i] != 1 || !ParseConfidence(v, !require_all))
                        return false;
                } else if (k == "aiParam." + labels[i] + ".confidenceConfig") {
                    if (++configs[i] != 1 || !ParseConfidenceConfig(v))
                        return false;
                }
            }
        if (require_all)
            for (auto n : seen)
                if (n != 1)
                    return false;
        return true;
    } catch (...) {
        return false;
    }
}
inline bool SleepReferenceModels(const std::vector<ModelInfo>& models) noexcept {
    try {
        struct Expected {
            const char* model;
            const char* label;
            const char* index;
            float strict, recommended;
        };
        const std::array<Expected, 3> expected{{{"1001003", "pedestrian", "0", .63f, .63f},
                                                {"1001010", "pedSleepStomach", "0", .954f, .9f},
                                                {"1001010", "pedSleepLieDown", "5", .98f, .55f}}};
        for (const auto& e : expected) {
            int count = 0;
            for (const auto& model : models)
                for (const auto& label : model.labels)
                    if (label.code == e.label) {
                        ++count;
                        if (model.id != e.model || label.label != e.index ||
                            !std::isfinite(label.confidenceHigh) || !std::isfinite(label.confidence) ||
                            label.confidenceHigh != e.strict || label.confidence != e.recommended)
                            return false;
                    }
            if (count != 1)
                return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
}  // namespace cosmo::sleep_candidate
#endif
