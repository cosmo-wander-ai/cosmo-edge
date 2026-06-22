// SystemParamDto — Picture quality set request

#include "SystemParamDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::System {
void to_json(nlohmann::json& j, const MsgSetPictureQualityRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["fullPictureQuality"] = v.fullPictureQuality;
    j["areaOverlay"]        = v.areaOverlay;
    j["targetBoxOverlay"]   = v.targetBoxOverlay;
    j["targetSizeOverlay"]  = v.targetSizeOverlay;
    j["alarmNameOverlay"]   = v.alarmNameOverlay;
}

void from_json(const nlohmann::json& j, MsgSetPictureQualityRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, fullPictureQuality);
    JSON_OPT(j, v, areaOverlay);
    JSON_OPT(j, v, targetBoxOverlay);
    JSON_OPT(j, v, targetSizeOverlay);
    JSON_OPT(j, v, alarmNameOverlay);
}

void to_json(nlohmann::json& j, const MsgQueryPictureQualitySend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryPictureQualitySend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSetAlarmVideoDurationRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["videoPreTime"]  = v.videoPreTime;
    j["videoPostTime"] = v.videoPostTime;
    j["switch"]        = v.enable;
}

void from_json(const nlohmann::json& j, MsgSetAlarmVideoDurationRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, videoPreTime);
    JSON_OPT(j, v, videoPostTime);
    JSON_OPT_KEY(j, v, "switch", enable);
}

void to_json(nlohmann::json& j, const MsgQueryAlarmVideoDurationSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryAlarmVideoDurationSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyDevRestartParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["isTimingRestart"] = v.isTimingRestart;
    j["weekDay"]         = v.weekDay;
    j["restartTime"]     = v.restartTime;
}

void from_json(const nlohmann::json& j, MsgModifyDevRestartParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("isTimingRestart").get_to(v.isTimingRestart);
    j.at("weekDay").get_to(v.weekDay);
    j.at("restartTime").get_to(v.restartTime);
}

void to_json(nlohmann::json& j, const MsgQueryDevRestartParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryDevRestartParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQuerySystemLogoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQuerySystemLogoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSetSystemLogoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["systemName"]    = v.systemName;
    j["logoFileType"]  = v.logoFileType;
    j["logoBase64"]    = v.logoBase64;
    j["bigScreenName"] = v.bigScreenName;
}

void from_json(const nlohmann::json& j, MsgSetSystemLogoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, systemName);
    JSON_OPT(j, v, logoFileType);
    JSON_OPT(j, v, logoBase64);
    JSON_OPT(j, v, bigScreenName);
}

void to_json(nlohmann::json& j, const MsgQueryPopUpParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryPopUpParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSetPopUpParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["popUpSwitch"]   = v.popUpSwitch;
    j["audioPlay"]     = v.audioPlay;
    j["popUpDuration"] = v.popUpDuration;
}

void from_json(const nlohmann::json& j, MsgSetPopUpParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, popUpSwitch);
    JSON_OPT(j, v, audioPlay);
    JSON_OPT(j, v, popUpDuration);
}

void to_json(nlohmann::json& j, const MsgSetResourceLimitParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["switch"] = v.enable;
}

void from_json(const nlohmann::json& j, MsgSetResourceLimitParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT_KEY(j, v, "switch", enable);
}

void to_json(nlohmann::json& j, const MsgQueryResourceLimitParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryResourceLimitParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgQueryPictureQualitySend::ResData& v) {
    JSON_OPT(j, v, fullPictureQuality);
    JSON_OPT(j, v, areaOverlay);
    JSON_OPT(j, v, targetBoxOverlay);
    JSON_OPT(j, v, alarmNameOverlay);
    JSON_OPT(j, v, targetSizeOverlay);
}

void to_json(nlohmann::json& j, const MsgQueryPictureQualitySend::ResData& v) {
    j["fullPictureQuality"] = v.fullPictureQuality;
    j["areaOverlay"]        = v.areaOverlay;
    j["targetBoxOverlay"]   = v.targetBoxOverlay;
    j["alarmNameOverlay"]   = v.alarmNameOverlay;
    j["targetSizeOverlay"]  = v.targetSizeOverlay;
}

void from_json(const nlohmann::json& j, AlarmVideoDurationData& v) {
    JSON_OPT(j, v, enable);
    JSON_OPT(j, v, videoPreTime);
    JSON_OPT(j, v, videoPostTime);
}

void to_json(nlohmann::json& j, const AlarmVideoDurationData& v) {
    j["enable"]        = v.enable;
    j["videoPreTime"]  = v.videoPreTime;
    j["videoPostTime"] = v.videoPostTime;
}

void from_json(const nlohmann::json& j, MsgQueryDevRestartParamSend::ResData& v) {
    JSON_OPT(j, v, isTimingRestart);
    JSON_OPT(j, v, weekDay);
    JSON_OPT(j, v, restartTime);
}

void to_json(nlohmann::json& j, const MsgQueryDevRestartParamSend::ResData& v) {
    j["isTimingRestart"] = v.isTimingRestart;
    j["weekDay"]         = v.weekDay;
    j["restartTime"]     = v.restartTime;
}

void from_json(const nlohmann::json& j, MsgQuerySystemLogoSend::ResData& v) {
    JSON_OPT(j, v, systemName);
    JSON_OPT(j, v, logoUrl);
    JSON_OPT(j, v, bigScreenName);
}

void to_json(nlohmann::json& j, const MsgQuerySystemLogoSend::ResData& v) {
    j["systemName"]    = v.systemName;
    j["logoUrl"]       = v.logoUrl;
    j["bigScreenName"] = v.bigScreenName;
}

void from_json(const nlohmann::json& j, MsgQueryPopUpParamSend::ResData& v) {
    JSON_OPT(j, v, popUpSwitch);
    JSON_OPT(j, v, popUpDuration);
    JSON_OPT(j, v, audioPlay);
}

void to_json(nlohmann::json& j, const MsgQueryPopUpParamSend::ResData& v) {
    j["popUpSwitch"]   = v.popUpSwitch;
    j["popUpDuration"] = v.popUpDuration;
    j["audioPlay"]     = v.audioPlay;
}

void from_json(const nlohmann::json& j, ResourceLimitParamData& v) {
    JSON_OPT(j, v, enable);
}

void to_json(nlohmann::json& j, const ResourceLimitParamData& v) {
    j["enable"] = v.enable;
}

}  // namespace cosmo::System
