// ClientMsgFile — Client-side message types

#include "service/path/dto/ClientMsgFile.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgReqGetFileServerConfig& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, CMsgReqGetFileServerConfig& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgFileServer& v) {
    JSON_OPT(j, v, fileServerUrl);
    JSON_OPT(j, v, user);
    JSON_OPT(j, v, token);
}

void to_json(nlohmann::json& j, const MsgFileServer& v) {
    j["fileServerUrl"] = v.fileServerUrl;
    j["user"]          = v.user;
    j["token"]         = v.token;
}

}  // namespace cosmo
