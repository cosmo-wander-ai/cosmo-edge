// ClientMsgAlgorithm — Client-side algorithm orchestration message types.

#include "ClientMsgAlgorithm.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const CMsgAlgorithmProcessConfigNGReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmCode"] = v.algorithmCode;
    j["devId"]         = v.devId;
}

void from_json(const nlohmann::json& j, CMsgAlgorithmProcessConfigNGReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, devId);
}

void to_json(nlohmann::json& j, const CMsgAlgorithmProcessConfigNGRsp& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, CMsgAlgorithmProcessConfigNGRsp& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const CMsgQueryPictureAlgorithmListReq& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["devId"] = v.devId;
}

void from_json(const nlohmann::json& j, CMsgQueryPictureAlgorithmListReq& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, devId);
}

void to_json(nlohmann::json& j, const CMsgQueryPictureAlgorithmListRsp& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, CMsgQueryPictureAlgorithmListRsp& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, PictureAlgorithmInfo& v) {
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmUpdateTime);
    JSON_OPT(j, v, algorithmCheckSum);
    JSON_OPT(j, v, algorithmId);
}

void to_json(nlohmann::json& j, const PictureAlgorithmInfo& v) {
    j["algorithmCode"]       = v.algorithmCode;
    j["algorithmName"]       = v.algorithmName;
    j["algorithmUpdateTime"] = v.algorithmUpdateTime;
    j["algorithmCheckSum"]   = v.algorithmCheckSum;
    j["algorithmId"]         = v.algorithmId;
}

void from_json(const nlohmann::json& j, CMsgQueryPictureAlgorithmListRsp::ResData& v) {
    JSON_OPT(j, v, nodeAlgorithmCheckSum);
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const CMsgQueryPictureAlgorithmListRsp::ResData& v) {
    j["nodeAlgorithmCheckSum"] = v.nodeAlgorithmCheckSum;
    j["list"]                  = v.list;
}

}  // namespace cosmo
