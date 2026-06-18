#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "util/dto/AlgorithmMsgTypes.h"
namespace cosmo::linkage {

inline constexpr std::string_view kLaAlarmDataCode   = "EVT_00001";
inline constexpr std::string_view kLaAudioDeviceCode = "DA_00001";

inline constexpr std::string_view kKeyStrageAlgs                 = "strageAlgorithms";
inline constexpr std::string_view kKeyStrageAudioDeviceId        = "deviceSN";
inline constexpr std::string_view kKeyStrageAudioDeviceOperation = "operation";
inline constexpr std::string_view kKeyStrageAudioDeviceData      = "data";
inline constexpr std::string_view kKeyStrageAudioDeviceText      = "dataText";
inline constexpr std::string_view kKeyStrageAudioDeviceTone      = "tone";
inline constexpr std::string_view kKeyStrageAudioDeviceSpeed     = "speed";
inline constexpr std::string_view kKeyStrageAudioDeviceVolume    = "volume";
inline constexpr std::string_view kKeyStrageAudioDeviceDuration  = "duration";
inline constexpr std::string_view kKeyStrageAudioDeviceTimes     = "times";
inline constexpr std::string_view kKeyStrageAudioDeviceGap       = "gap";

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
