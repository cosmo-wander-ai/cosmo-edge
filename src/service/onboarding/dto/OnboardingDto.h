/// @file OnboardingDto.h
/// @brief DTO message types for the Onboarding Wizard feature.
///        All responses are abstract — no physical filesystem paths are exposed.
#pragma once

#include <string>

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

// ── Start demo sandbox ───────────────────────────────────────────
// Request body is intentionally empty — the backend internally resolves
// the built-in demo video and model paths. No path parameters accepted.

struct MsgStartDemoRecv : public MsgRecvHead {};

struct MsgStartDemoSend : public MsgSendHead {
    struct ResData {
        std::string camera_id;
        std::string camera_name;
        std::string algorithm_code;
    } res_data;
};

void to_json(nlohmann::json& j, const MsgStartDemoSend::ResData& v);
void from_json(const nlohmann::json& j, MsgStartDemoSend::ResData& v);
void to_json(nlohmann::json& j, const MsgStartDemoSend& v);
void from_json(const nlohmann::json& j, MsgStartDemoSend& v);

// ── Complete onboarding ──────────────────────────────────────────

struct MsgCompleteRecv : public MsgRecvHead {};

struct MsgCompleteSend : public MsgSendHead {};

void to_json(nlohmann::json& j, const MsgCompleteSend& v);
void from_json(const nlohmann::json& j, MsgCompleteSend& v);

// ── Reset demo sandbox ───────────────────────────────────────────

struct MsgResetDemoRecv : public MsgRecvHead {};

struct MsgResetDemoSend : public MsgSendHead {};

void to_json(nlohmann::json& j, const MsgResetDemoSend& v);
void from_json(const nlohmann::json& j, MsgResetDemoSend& v);

}  // namespace Onboarding
}  // namespace cosmo
