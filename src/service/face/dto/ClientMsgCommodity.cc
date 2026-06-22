// ClientMsgCommodity — Client-side commodity/things lib message types.

#include "ClientMsgCommodity.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgFeatureSetInfo& v) {
    j["version"]   = v.version;
    j["threshold"] = v.threshold;
    j["name"]      = v.name;
    j["list"]      = v.list;
}

void from_json(const nlohmann::json& j, CMsgFeatureSetInfo& v) {
    j.at("version").get_to(v.version);
    j.at("threshold").get_to(v.threshold);
    j.at("name").get_to(v.name);
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const CMsgQueryCommoditySetReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["setToken"]       = v.setToken;
    j["currentVersion"] = v.currentVersion;
}

void from_json(const nlohmann::json& j, CMsgQueryCommoditySetReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, setToken);
    JSON_OPT(j, v, currentVersion);
}

void to_json(nlohmann::json& j, const CMsgQueryCommoditySetRsp& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, CMsgQueryCommoditySetRsp& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, CMsgFeatureSetElement& v) {
    JSON_OPT(j, v, token);
    JSON_OPT(j, v, url);
    JSON_OPT(j, v, status);
}

void to_json(nlohmann::json& j, const CMsgFeatureSetElement& v) {
    j["token"]  = v.token;
    j["url"]    = v.url;
    j["status"] = v.status;
}

}  // namespace cosmo
