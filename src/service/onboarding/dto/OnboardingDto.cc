/// @file OnboardingDto.cc
/// @brief nlohmann::json serialization for Onboarding DTO types.
#include "service/onboarding/dto/OnboardingDto.h"

#include <nlohmann/json.hpp>

namespace cosmo::Onboarding {

// ── MsgStatusSend ────────────────────────────────────────────────

void to_json(nlohmann::json& j, const MsgStatusSend::ResData& v) {
    j = nlohmann::json{{"onboardingCompleted", v.onboarding_completed}};
}

void from_json(const nlohmann::json& j, MsgStatusSend::ResData& v) {
    j.at("onboardingCompleted").get_to(v.onboarding_completed);
}

void to_json(nlohmann::json& j, const MsgStatusSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.res_data;
}

void from_json(const nlohmann::json& j, MsgStatusSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    j.at("resData").get_to(v.res_data);
}

// ── MsgCompleteSend ──────────────────────────────────────────────

void to_json(nlohmann::json& j, const MsgCompleteSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
}

void from_json(const nlohmann::json& j, MsgCompleteSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
}

// ── MsgResetSend ────────────────────────────────────────────────

void to_json(nlohmann::json& j, const MsgResetSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
}

void from_json(const nlohmann::json& j, MsgResetSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
}

}  // namespace cosmo::Onboarding
