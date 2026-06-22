// ClientMsgCollect — Client-side collection event message types.

#include "service/event/dto/ClientMsgCollect.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgCollectRptReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["messageId"]      = v.messageId;
    j["devId"]          = v.devId;
    j["taskId"]         = v.taskId;
    j["videoChannelId"] = v.videoChannelId;
    j["timestamp"]      = v.timestamp;
    j["algorithmId"]    = v.algorithmId;
    j["algorithmCode"]  = v.algorithmCode;
    j["algorithmName"]  = v.algorithmName;
    j["orignalPicture"] = v.orignalPicture;
    j["lableList"]      = v.lableList;
}

void from_json(const nlohmann::json& j, CMsgCollectRptReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, messageId);
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, taskId);
    JSON_OPT(j, v, videoChannelId);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, orignalPicture);
    JSON_OPT(j, v, lableList);
}

void from_json(const nlohmann::json& j, CMsgLabelObject& v) {
    JSON_OPT(j, v, atomicCode);
    JSON_OPT(j, v, lableName);
    JSON_OPT(j, v, confidence);
    JSON_OPT(j, v, rectangle);
}

void to_json(nlohmann::json& j, const CMsgLabelObject& v) {
    j["atomicCode"] = v.atomicCode;
    j["lableName"]  = v.lableName;
    j["confidence"] = v.confidence;
    j["rectangle"]  = v.rectangle;
}

}  // namespace cosmo
