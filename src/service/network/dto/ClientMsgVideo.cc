// ClientMsgVideo — Client-side video playback message types.

#include "ClientMsgVideo.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgGetVideoPlayReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devId"]          = v.devId;
    j["videoChannelId"] = v.videoChannelId;
}

void from_json(const nlohmann::json& j, CMsgGetVideoPlayReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, videoChannelId);
}

void to_json(nlohmann::json& j, const CMsgGetVideoPlayRsp& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, CMsgGetVideoPlayRsp& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, CMsgGetVideoPlayRsp::ResData& v) {
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, videoChannelId);
    JSON_OPT(j, v, videoChannelName);
    JSON_OPT(j, v, streamUrl);
}

void to_json(nlohmann::json& j, const CMsgGetVideoPlayRsp::ResData& v) {
    j["devId"]            = v.devId;
    j["videoChannelId"]   = v.videoChannelId;
    j["videoChannelName"] = v.videoChannelName;
    j["streamUrl"]        = v.streamUrl;
}

}  // namespace cosmo
