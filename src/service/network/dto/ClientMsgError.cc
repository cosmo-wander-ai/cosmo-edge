// ClientMsgError — Client-side error event message types.

#include "ClientMsgError.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgOnErrorsReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devId"]        = v.devId;
    j["taskId"]       = v.taskId;
    j["timestamp"]    = v.timestamp;
    j["details"]      = v.details;
    j["actionStatus"] = v.actionStatus;
    j["actionInfos"]  = v.actionInfos;
}

void from_json(const nlohmann::json& j, CMsgOnErrorsReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, taskId);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, details);
    JSON_OPT(j, v, actionStatus);
    JSON_OPT(j, v, actionInfos);
}

void from_json(const nlohmann::json& j, OnErrorsReason& v) {
    JSON_OPT(j, v, aiHostId);
    JSON_OPT(j, v, aiHostIp);
    JSON_OPT(j, v, videoChannelId);
    JSON_OPT(j, v, videoChannelName);
    JSON_OPT(j, v, streamUrl);
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmVersion);
    JSON_OPT(j, v, message);
}

void to_json(nlohmann::json& j, const OnErrorsReason& v) {
    j["aiHostId"]         = v.aiHostId;
    j["aiHostIp"]         = v.aiHostIp;
    j["videoChannelId"]   = v.videoChannelId;
    j["videoChannelName"] = v.videoChannelName;
    j["streamUrl"]        = v.streamUrl;
    j["algorithmId"]      = v.algorithmId;
    j["algorithmName"]    = v.algorithmName;
    j["algorithmVersion"] = v.algorithmVersion;
    j["message"]          = v.message;
}

void from_json(const nlohmann::json& j, ActionInfoSonMsg& v) {
    JSON_OPT(j, v, channelId);
    JSON_OPT(j, v, taskId);
    JSON_OPT(j, v, actionId);
    JSON_OPT(j, v, fps);
    JSON_OPT(j, v, queueName);
}

void to_json(nlohmann::json& j, const ActionInfoSonMsg& v) {
    j["channelId"] = v.channelId;
    j["taskId"]    = v.taskId;
    j["actionId"]  = v.actionId;
    j["fps"]       = v.fps;
    j["queueName"] = v.queueName;
}

void from_json(const nlohmann::json& j, ActionInfoMsg& v) {
    JSON_OPT(j, v, actionId);
    JSON_OPT(j, v, channelId);
    JSON_OPT(j, v, bChannelReuse);
    JSON_OPT(j, v, maxTaskFps);
    JSON_OPT(j, v, queueName);
    JSON_OPT(j, v, sons);
}

void to_json(nlohmann::json& j, const ActionInfoMsg& v) {
    j["actionId"]      = v.actionId;
    j["channelId"]     = v.channelId;
    j["bChannelReuse"] = v.bChannelReuse;
    j["maxTaskFps"]    = v.maxTaskFps;
    j["queueName"]     = v.queueName;
    j["sons"]          = v.sons;
}

void from_json(const nlohmann::json& j, OnErrorsDetail& v) {
    JSON_OPT(j, v, type);
    JSON_OPT(j, v, reason);
}

void to_json(nlohmann::json& j, const OnErrorsDetail& v) {
    j["type"]   = v.type;
    j["reason"] = v.reason;
}

}  // namespace cosmo
