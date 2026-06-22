// CameraMsgTypes — Camera types — MsgCameraAttr, MsgCameraInfo, ChannelStatus, MsgBaseCameraTask.

#include "CameraMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgBaseCameraTask& v) {
    j["cameraId"]      = v.cameraId;
    j["algorithmCode"] = v.algorithmCode;
}

void from_json(const nlohmann::json& j, MsgBaseCameraTask& v) {
    j.at("cameraId").get_to(v.cameraId);
    j.at("algorithmCode").get_to(v.algorithmCode);
}

void to_json(nlohmann::json& j, const MsgCameraTask& v) {
    j["algorithmId"]   = v.algorithmId;
    j["algorithmName"] = v.algorithmName;
    j["scheduleId"]    = v.scheduleId;
    j["scheduleName"]  = v.scheduleName;
    j["status"]        = v.status;
    j["enableStatus"]  = v.enable;
}

void from_json(const nlohmann::json& j, MsgCameraTask& v) {
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, scheduleId);
    JSON_OPT(j, v, scheduleName);
    JSON_OPT(j, v, status);
    JSON_OPT_KEY(j, v, "enableStatus", enable);
}

void to_json(nlohmann::json& j, const MsgCameraInfo& v) {
    to_json(j, static_cast<const MsgCameraAttr&>(v));
    j["videoChannelId"] = v.videoChannelId;
    j["channelCode"]    = v.channelCode;
    j["channelType"]    = v.channelType;
    j["url"]            = v.url;
    j["channelName"]    = v.channelName;
    j["channelPic"]     = v.channelPic;
    j["taskList"]       = v.taskList;
}

void from_json(const nlohmann::json& j, MsgCameraInfo& v) {
    from_json(j, static_cast<MsgCameraAttr&>(v));
    JSON_OPT(j, v, videoChannelId);
    JSON_OPT(j, v, channelCode);
    JSON_OPT(j, v, channelType);
    JSON_OPT(j, v, url);
    JSON_OPT(j, v, channelName);
    JSON_OPT(j, v, channelPic);
    JSON_OPT(j, v, taskList);
}

void from_json(const nlohmann::json& j, MsgCameraAttr& v) {
    JSON_OPT(j, v, width);
    JSON_OPT(j, v, height);
    JSON_OPT(j, v, codec);
    JSON_OPT(j, v, fps);
    JSON_OPT(j, v, channelStatus);
}

void to_json(nlohmann::json& j, const MsgCameraAttr& v) {
    j["width"]         = v.width;
    j["height"]        = v.height;
    j["codec"]         = v.codec;
    j["fps"]           = v.fps;
    j["channelStatus"] = v.channelStatus;
}

}  // namespace cosmo
