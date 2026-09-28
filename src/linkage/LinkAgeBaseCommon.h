#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "util/dto/AlgorithmMsgTypes.h"
namespace cosmo::linkage {

// IDs emitted by data/resource/*/layout/linkageStorages.json.
inline constexpr std::string_view kLaAlarmDataCode   = "LA_AlarmData_Code";
inline constexpr std::string_view kLaAudioDeviceCode = "LA_AudioDevice_Code";
inline constexpr std::string_view kLaAlarmOutputCode = "LA_AlarmOutput_Code";
// Keep already-persisted strategies from older releases readable.
inline constexpr std::string_view kLaAlarmDataLegacyCode   = "EVT_00001";
inline constexpr std::string_view kLaAudioDeviceLegacyCode = "DA_00001";

enum class LinkAgeActionKind {
    kUnsupported,
    kAlarm,
    kAudioDevice,
    kAlarmOutput,
};

constexpr LinkAgeActionKind ClassifyLinkAgeActionId(std::string_view action_id) {
    if (action_id == kLaAlarmDataCode || action_id == kLaAlarmDataLegacyCode) {
        return LinkAgeActionKind::kAlarm;
    }
    if (action_id == kLaAudioDeviceCode || action_id == kLaAudioDeviceLegacyCode) {
        return LinkAgeActionKind::kAudioDevice;
    }
    if (action_id == kLaAlarmOutputCode) {
        return LinkAgeActionKind::kAlarmOutput;
    }
    return LinkAgeActionKind::kUnsupported;
}

inline constexpr std::string_view kKeyStrageAlgs                 = "strageAlgorithms";
inline constexpr std::string_view kKeyLinkageAlgs                = "algs";
inline constexpr std::string_view kKeyStrageAudioDeviceId        = "deviceSN";
inline constexpr std::string_view kKeyLinkageAudioDeviceId       = "audioDeviceId";
inline constexpr std::string_view kKeyStrageAudioDeviceOperation = "operation";
inline constexpr std::string_view kKeyStrageAudioDeviceData      = "data";
inline constexpr std::string_view kKeyStrageAudioDeviceText      = "dataText";
inline constexpr std::string_view kKeyLinkageAudioDeviceText     = "text";
inline constexpr std::string_view kKeyStrageAudioDeviceTone      = "tone";
inline constexpr std::string_view kKeyStrageAudioDeviceSpeed     = "speed";
inline constexpr std::string_view kKeyStrageAudioDeviceVolume    = "volume";
inline constexpr std::string_view kKeyStrageAudioDeviceDuration  = "duration";
inline constexpr std::string_view kKeyStrageAudioDeviceTimes     = "times";
inline constexpr std::string_view kKeyStrageAudioDeviceGap       = "gap";

constexpr bool IsAlarmAlgorithmsKey(std::string_view key) {
    return key == kKeyStrageAlgs || key == kKeyLinkageAlgs;
}

constexpr bool IsAudioDeviceIdKey(std::string_view key) {
    return key == kKeyStrageAudioDeviceId || key == kKeyLinkageAudioDeviceId;
}

constexpr bool IsAudioTextKey(std::string_view key) {
    return key == kKeyStrageAudioDeviceText || key == kKeyLinkageAudioDeviceText;
}

struct LinkAgeParamNode : public ActionBase {
    std::string action_id;
    std::string action_name;
    MvActionConfigObject config_object;
};

void to_json(nlohmann::json& j, const LinkAgeParamNode& v);
void from_json(const nlohmann::json& j, LinkAgeParamNode& v);

struct LinkageStrategyWorkflow {
    std::vector<LinkAgeParamNode> workflow;
    friend void to_json(nlohmann::json& j, const LinkageStrategyWorkflow& v);
    friend void from_json(const nlohmann::json& j, LinkageStrategyWorkflow& v);
};

}  // namespace cosmo::linkage
