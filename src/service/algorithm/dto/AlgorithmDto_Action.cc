// AlgorithmDto_Action — Algorithm Dto_ Action implementation.

#include <nlohmann/json.hpp>

#include "AlgorithmDto.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Atomic action and passenger flow serialization (split from AlgorithmDto.cc)
namespace cosmo::Algorithm {

void to_json(nlohmann::json& j, const MsgAtomicActionListRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["actionUsage"] = v.actionUsage;
    j["filePath"]    = v.filePath;
}

void from_json(const nlohmann::json& j, MsgAtomicActionListRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, actionUsage);
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgAtomicActionListSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgAtomicActionListSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgPassFlowListSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgPassFlowListSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgAtomicAction& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, actionName);
    JSON_OPT(j, v, inputParamConfig);
    JSON_OPT(j, v, actionUsage);
    JSON_OPT(j, v, actionType);
}

void to_json(nlohmann::json& j, const MsgAtomicAction& v) {
    j["id"]               = v.id;
    j["name"]             = v.name;
    j["actionName"]       = v.actionName;
    j["inputParamConfig"] = v.inputParamConfig;
    j["actionUsage"]      = v.actionUsage;
    j["actionType"]       = v.actionType;
}

void from_json(const nlohmann::json& j, MsgAtomicActionListSend::ResData& v) {
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const MsgAtomicActionListSend::ResData& v) {
    j["list"] = v.list;
}

void from_json(const nlohmann::json& j, MsgPassFlowListSend::MsgPassFlowUnit& v) {
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmName);
}

void to_json(nlohmann::json& j, const MsgPassFlowListSend::MsgPassFlowUnit& v) {
    j["algorithmId"]   = v.algorithmId;
    j["algorithmName"] = v.algorithmName;
}

void from_json(const nlohmann::json& j, MsgPassFlowListSend::ResData& v) {
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const MsgPassFlowListSend::ResData& v) {
    j["list"] = v.list;
}

}  // namespace cosmo::Algorithm
