// ClientMsgInfo — Client-side info event message types.

#include "service/event/dto/ClientMsgInfo.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgOnInfoReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["type"]      = v.type;
    j["devId"]     = v.devId;
    j["timestamp"] = v.timestamp;
    j["details"]   = v.details;
}

void from_json(const nlohmann::json& j, CMsgOnInfoReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, type);
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, details);
}

void from_json(const nlohmann::json& j, CMsgOnInfoDetail& v) {
    JSON_OPT(j, v, taskId);
    JSON_OPT(j, v, message);
    JSON_OPT(j, v, videoChannelId);
    JSON_OPT(j, v, resolution);
    JSON_OPT(j, v, videoEncoding);
    JSON_OPT(j, v, fps);
}

void to_json(nlohmann::json& j, const CMsgOnInfoDetail& v) {
    j["taskId"]         = v.taskId;
    j["message"]        = v.message;
    j["videoChannelId"] = v.videoChannelId;
    j["resolution"]     = v.resolution;
    j["videoEncoding"]  = v.videoEncoding;
    j["fps"]            = v.fps;
}

}  // namespace cosmo
