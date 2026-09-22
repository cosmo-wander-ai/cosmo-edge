#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "util/MsgDynamicElement.h"

namespace cosmo::util {

inline constexpr std::string_view kPrivacyEnabled  = "param.privacyEnabled";
inline constexpr std::string_view kPrivacyLabels   = "param.privacyLabels";
inline constexpr std::string_view kPrivacyStrength = "param.privacyStrength";

inline bool IsAlarmImagePrivacyKey(std::string_view key) {
    return key == kPrivacyEnabled || key == kPrivacyLabels || key == kPrivacyStrength;
}

// Values remain strings so a malformed persisted policy cannot silently disable privacy.
struct AlarmImagePrivacy {
    std::string enabled{"0"};
    std::string labels{"*"};
    std::string strength{"2"};

    bool Requested() const {
        return enabled != "0";
    }

    bool Apply(std::string_view key, const std::string& value) {
        if (key == kPrivacyEnabled) {
            enabled = value;
        } else if (key == kPrivacyLabels) {
            labels = value;
        } else if (key == kPrivacyStrength) {
            strength = value;
        } else {
            return false;
        }
        return true;
    }

    bool Valid() const {
        if ((enabled != "0" && enabled != "1") || (strength != "1" && strength != "2" && strength != "3")) {
            return false;
        }
        if (labels == "*") {
            return true;
        }
        if (labels.empty() || labels.front() == ',' || labels.back() == ',') {
            return false;
        }
        bool separator = false;
        for (unsigned char c : labels) {
            if (c <= ' ' || c == 127 || c == '*' || (c == ',' && separator)) {
                return false;
            }
            separator = c == ',';
        }
        return true;
    }

    bool Selects(std::string_view label) const {
        if (labels == "*") {
            return true;
        }
        size_t begin = 0;
        while (begin < labels.size()) {
            const auto end   = labels.find(',', begin);
            const auto token = std::string_view(labels).substr(
                begin, end == std::string::npos ? std::string::npos : end - begin);
            if (token == label) {
                return true;
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
        return false;
    }

    int Strength() const {
        return strength == "1" ? 1 : strength == "3" ? 3 : 2;
    }
};

inline bool ValidateAlarmImagePrivacyParams(const std::vector<MsgDynamicKeyValue>& params) {
    AlarmImagePrivacy policy;
    for (const auto& param : params) {
        policy.Apply(param.key.ToRefString(), param.value.ToString());
    }
    return policy.Valid();
}

}  // namespace cosmo::util
