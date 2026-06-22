// ModelDto_Component — Model Dto_ Component implementation.

#include <nlohmann/json.hpp>

#include "ModelDto.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Component and atomic model serialization (split from ModelDto.cc)
namespace cosmo::Model {

void from_json(const nlohmann::json& j, MsgModelComponent& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, componentName);
    JSON_OPT(j, v, componentType);
    JSON_OPT(j, v, inputParamConfig);
}

void to_json(nlohmann::json& j, const MsgModelComponent& v) {
    j["id"]               = v.id;
    j["componentName"]    = v.componentName;
    j["componentType"]    = v.componentType;
    j["inputParamConfig"] = v.inputParamConfig;
}

void from_json(const nlohmann::json& j, MsgGetModelComponentsSend::ResData& v) {
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const MsgGetModelComponentsSend::ResData& v) {
    j["list"] = v.list;
}

void from_json(const nlohmann::json& j, MsgAtomicModelLabel& v) {
    JSON_OPT(j, v, label);
    JSON_OPT(j, v, nameCN);
    JSON_OPT(j, v, threshold);
    JSON_OPT(j, v, class_name);
    JSON_OPT(j, v, used);
}

void to_json(nlohmann::json& j, const MsgAtomicModelLabel& v) {
    j["label"]      = v.label;
    j["nameCN"]     = v.nameCN;
    j["threshold"]  = v.threshold;
    j["class_name"] = v.class_name;
    j["used"]       = v.used;
}

void from_json(const nlohmann::json& j, MsgAtomicModel& v) {
    JSON_OPT(j, v, atomicCode);
    JSON_OPT(j, v, atomicName);
    JSON_OPT(j, v, label);
    JSON_OPT(j, v, labelList);
}

void to_json(nlohmann::json& j, const MsgAtomicModel& v) {
    j["atomicCode"] = v.atomicCode;
    j["atomicName"] = v.atomicName;
    j["label"]      = v.label;
    j["labelList"]  = v.labelList;
}

void from_json(const nlohmann::json& j, MsgListSend::ResData& v) {
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const MsgListSend::ResData& v) {
    j["list"] = v.list;
}

}  // namespace cosmo::Model
