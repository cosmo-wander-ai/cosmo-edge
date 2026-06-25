/// @file OnboardingDto.h
/// @brief DTO message types for the Onboarding Wizard feature.
#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {
namespace Onboarding {

// ── Query onboarding status ──────────────────────────────────────

struct MsgStatusRecv : public MsgRecvHead {};

struct MsgStatusSend : public MsgSendHead {
    struct ResData {
        bool onboarding_completed{false};
    } res_data;
};

void to_json(nlohmann::json& j, const MsgStatusSend::ResData& v);
void from_json(const nlohmann::json& j, MsgStatusSend::ResData& v);
void to_json(nlohmann::json& j, const MsgStatusSend& v);
void from_json(const nlohmann::json& j, MsgStatusSend& v);

// ── Complete onboarding ──────────────────────────────────────────

struct MsgCompleteRecv : public MsgRecvHead {};

struct MsgCompleteSend : public MsgSendHead {};

void to_json(nlohmann::json& j, const MsgCompleteSend& v);
void from_json(const nlohmann::json& j, MsgCompleteSend& v);

// ── Reset onboarding ─────────────────────────────────────────────

struct MsgResetRecv : public MsgRecvHead {};

struct MsgResetSend : public MsgSendHead {};

void to_json(nlohmann::json& j, const MsgResetSend& v);
void from_json(const nlohmann::json& j, MsgResetSend& v);

}  // namespace Onboarding
}  // namespace cosmo
