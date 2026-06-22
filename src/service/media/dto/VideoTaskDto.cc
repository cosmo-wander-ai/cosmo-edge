// VideoTaskDto — VideoTask DTO definitions (extracted from MessageVideoTaskHandler.h)

#include "VideoTaskDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Config management serialization (param/area/strategy)
// (Switch/Delete in VideoTaskDto_Switch.cc, Query/Channel in VideoTaskDto_Query.cc)
namespace cosmo::VideoTask {
void to_json(nlohmann::json& j, const MsgChannelTask& v) {
    j["channelId"]   = v.channelId;
    j["algorithmId"] = v.algorithmId;
    j["id"]          = v.id;
}

void from_json(const nlohmann::json& j, MsgChannelTask& v) {
    j.at("channelId").get_to(v.channelId);
    j.at("algorithmId").get_to(v.algorithmId);
    JSON_OPT(j, v, id);
}

void to_json(nlohmann::json& j, const MsgModifyParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
    j["taskConfig"] = v.taskConfig;
    j["scheduleId"] = v.scheduleId;
}

void from_json(const nlohmann::json& j, MsgModifyParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
    JSON_OPT(j, v, taskConfig);
    JSON_OPT(j, v, scheduleId);
}

void to_json(nlohmann::json& j, const MsgQueryParamRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryParamRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryParamSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryParamSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyAreaRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
    j["areas"]         = v.areas;
    j["shieldedAreas"] = v.shieldedAreas;
}

void from_json(const nlohmann::json& j, MsgModifyAreaRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
    JSON_OPT(j, v, areas);
    JSON_OPT(j, v, shieldedAreas);
}

void to_json(nlohmann::json& j, const MsgQueryAreaRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryAreaRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryAreaSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryAreaSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyStrategyRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
    j["videoRepeatCount"] = v.videoRepeatCount;
    j["scheduleId"]       = v.scheduleId;
}

void from_json(const nlohmann::json& j, MsgModifyStrategyRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
    JSON_OPT(j, v, videoRepeatCount);
    JSON_OPT(j, v, scheduleId);
}

void to_json(nlohmann::json& j, const MsgQueryStrategyRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryStrategyRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryStrategySend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryStrategySend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgSaveOrUpdateRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
    j["taskConfig"] = v.taskConfig;
    j["scheduleId"] = v.scheduleId;
}

void from_json(const nlohmann::json& j, MsgSaveOrUpdateRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
    JSON_OPT(j, v, taskConfig);
    JSON_OPT(j, v, scheduleId);
}

void to_json(nlohmann::json& j, const MsgSelectConfigByAlgorithmIdRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgChannelTask&>(v));
}

void from_json(const nlohmann::json& j, MsgSelectConfigByAlgorithmIdRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgChannelTask&>(v));
}

void to_json(nlohmann::json& j, const MsgSelectConfigByAlgorithmIdSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgSelectConfigByAlgorithmIdSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgQueryParamSend::ResData& v) {
    JSON_OPT(j, v, params);
}

void to_json(nlohmann::json& j, const MsgQueryParamSend::ResData& v) {
    j["params"] = v.params;
}

void from_json(const nlohmann::json& j, MsgQueryAreaSend::ResData& v) {
    JSON_OPT(j, v, areas);
    JSON_OPT(j, v, shieldedAreas);
}

void to_json(nlohmann::json& j, const MsgQueryAreaSend::ResData& v) {
    j["areas"]         = v.areas;
    j["shieldedAreas"] = v.shieldedAreas;
}

void from_json(const nlohmann::json& j, MsgQueryStrategySend::ResData& v) {
    JSON_OPT(j, v, videoRepeatCount);
    JSON_OPT(j, v, scheduleId);
}

void to_json(nlohmann::json& j, const MsgQueryStrategySend::ResData& v) {
    j["videoRepeatCount"] = v.videoRepeatCount;
    j["scheduleId"]       = v.scheduleId;
}

void from_json(const nlohmann::json& j, MsgSelectConfigByAlgorithmIdSend::ResData& v) {
    JSON_OPT(j, v, taskConfig);
    JSON_OPT(j, v, scheduleId);
    JSON_OPT(j, v, algorithmMetadata);
    JSON_OPT(j, v, taskStatus);
    JSON_OPT(j, v, taskEnableStatus);
}

void to_json(nlohmann::json& j, const MsgSelectConfigByAlgorithmIdSend::ResData& v) {
    j["taskConfig"]        = v.taskConfig;
    j["scheduleId"]        = v.scheduleId;
    j["algorithmMetadata"] = v.algorithmMetadata;
    j["taskStatus"]        = v.taskStatus;
    j["taskEnableStatus"]  = v.taskEnableStatus;
}

}  // namespace cosmo::VideoTask
