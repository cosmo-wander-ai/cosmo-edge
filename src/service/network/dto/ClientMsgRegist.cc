// ClientMsgRegist — Client-side registration message types.

#include "ClientMsgRegist.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgRegistReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["aiHostUrl"]     = v.aiHostUrl;
    j["devId"]         = v.devId;
    j["engineType"]    = v.engineType;
    j["supplier"]      = v.supplier;
    j["aiHostVersion"] = v.aiHostVersion;
    j["analysisType"]  = v.analysisType;
    j["devType"]       = v.devType;
}

void from_json(const nlohmann::json& j, CMsgRegistReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("aiHostUrl").get_to(v.aiHostUrl);
    j.at("devId").get_to(v.devId);
    j.at("engineType").get_to(v.engineType);
    j.at("supplier").get_to(v.supplier);
    j.at("aiHostVersion").get_to(v.aiHostVersion);
    j.at("analysisType").get_to(v.analysisType);
    j.at("devType").get_to(v.devType);
}

void to_json(nlohmann::json& j, const CMsgRegistRsp& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, CMsgRegistRsp& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, CMsgRegistConfig& v) {
    JSON_OPT(j, v, ntpServerUrl);
    JSON_OPT(j, v, h265TransServerUrl);
    JSON_OPT(j, v, tryPullFlowCounts);
    JSON_OPT(j, v, bodyTrackParameter);
    JSON_OPT(j, v, packetDiscardRatio);
    JSON_OPT(j, v, fileServer);
}

void to_json(nlohmann::json& j, const CMsgRegistConfig& v) {
    j["ntpServerUrl"]       = v.ntpServerUrl;
    j["h265TransServerUrl"] = v.h265TransServerUrl;
    j["tryPullFlowCounts"]  = v.tryPullFlowCounts;
    j["bodyTrackParameter"] = v.bodyTrackParameter;
    j["packetDiscardRatio"] = v.packetDiscardRatio;
    j["fileServer"]         = v.fileServer;
}

void from_json(const nlohmann::json& j, CMsgRegistRsp::ResData& v) {
    JSON_OPT(j, v, devId);
    JSON_OPT(j, v, devName);
    JSON_OPT(j, v, managerUrl);
    JSON_OPT(j, v, heartBeatPeriod);
    JSON_OPT(j, v, appKey);
    JSON_OPT(j, v, appSecret);
    JSON_OPT(j, v, initAlgorithmList);
    JSON_OPT(j, v, config);
}

void to_json(nlohmann::json& j, const CMsgRegistRsp::ResData& v) {
    j["devId"]             = v.devId;
    j["devName"]           = v.devName;
    j["managerUrl"]        = v.managerUrl;
    j["heartBeatPeriod"]   = v.heartBeatPeriod;
    j["appKey"]            = v.appKey;
    j["appSecret"]         = v.appSecret;
    j["initAlgorithmList"] = v.initAlgorithmList;
    j["config"]            = v.config;
}

}  // namespace cosmo
