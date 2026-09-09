// AlgorithmPacketDto — Algorithm packet data types — extracted from flow/action/AlgorithmPacketMng.h

#include "AlgorithmPacketDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::service::algorithm {
void from_json(const nlohmann::json& j, AlgorithmPacketInfo& v) {
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmMetadata);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmProcessdata);
    JSON_OPT(j, v, algorithmSource);
    JSON_OPT(j, v, algorithmUpdateTime);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, atomicList);
    JSON_OPT(j, v, confVersionId);
    JSON_OPT(j, v, confVersionName);
    JSON_OPT(j, v, configType);
    JSON_OPT(j, v, createTime);
    JSON_OPT(j, v, creator);
    JSON_OPT(j, v, eventType);
    JSON_OPT(j, v, gafAlgorithmId);
    JSON_OPT(j, v, gafAlgorithmName);
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, isDelete);
    JSON_OPT(j, v, packageAlgorithmName);
    JSON_OPT(j, v, pollingSupport);
    JSON_OPT(j, v, remark);
    JSON_OPT(j, v, status);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, updateTime);
    JSON_OPT(j, v, updator);
    JSON_OPT(j, v, visualized);
}

void to_json(nlohmann::json& j, const AlgorithmPacketInfo& v) {
    j["algorithmCategory"]    = v.algorithmCategory;
    j["algorithmCode"]        = v.algorithmCode;
    j["algorithmMetadata"]    = v.algorithmMetadata;
    j["algorithmName"]        = v.algorithmName;
    j["algorithmProcessdata"] = v.algorithmProcessdata;
    j["algorithmSource"]      = v.algorithmSource;
    j["algorithmUpdateTime"]  = v.algorithmUpdateTime;
    j["algorithmUsage"]       = v.algorithmUsage;
    j["atomicList"]           = v.atomicList;
    j["confVersionId"]        = v.confVersionId;
    j["confVersionName"]      = v.confVersionName;
    j["configType"]           = v.configType;
    j["createTime"]           = v.createTime;
    j["creator"]              = v.creator;
    j["eventType"]            = v.eventType;
    j["gafAlgorithmId"]       = v.gafAlgorithmId;
    j["gafAlgorithmName"]     = v.gafAlgorithmName;
    j["id"]                   = v.id;
    j["isDelete"]             = v.isDelete;
    j["packageAlgorithmName"] = v.packageAlgorithmName;
    j["pollingSupport"]       = v.pollingSupport;
    j["remark"]               = v.remark;
    j["status"]               = v.status;
    j["supplier"]             = v.supplier;
    j["updateTime"]           = v.updateTime;
    j["updator"]              = v.updator;
    j["visualized"]           = v.visualized;
}

void from_json(const nlohmann::json& j, LayoutDetailVersion& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmMetadata);
    JSON_OPT(j, v, algorithmProcessdata);
    JSON_OPT(j, v, atomicList);
    JSON_OPT(j, v, algorithmUpdateTime);
}

void to_json(nlohmann::json& j, const LayoutDetailVersion& v) {
    j["id"]                   = v.id;
    j["name"]                 = v.name;
    j["algorithmCode"]        = v.algorithmCode;
    j["algorithmMetadata"]    = v.algorithmMetadata;
    j["algorithmProcessdata"] = v.algorithmProcessdata;
    j["atomicList"]           = v.atomicList;
    j["algorithmUpdateTime"]  = v.algorithmUpdateTime;
}

void from_json(const nlohmann::json& j, LayoutDetailResult& v) {
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, remark);
    JSON_OPT(j, v, confVersionId);
    JSON_OPT(j, v, algorithmMetadata);
    JSON_OPT(j, v, algorithmProcessdata);
    JSON_OPT(j, v, atomicList);
    JSON_OPT(j, v, configVersionList);
}

void to_json(nlohmann::json& j, const LayoutDetailResult& v) {
    j["algorithmCode"]        = v.algorithmCode;
    j["algorithmName"]        = v.algorithmName;
    j["algorithmCategory"]    = v.algorithmCategory;
    j["algorithmUsage"]       = v.algorithmUsage;
    j["supplier"]             = v.supplier;
    j["remark"]               = v.remark;
    j["confVersionId"]        = v.confVersionId;
    j["algorithmMetadata"]    = v.algorithmMetadata;
    j["algorithmProcessdata"] = v.algorithmProcessdata;
    j["atomicList"]           = v.atomicList;
    j["configVersionList"]    = v.configVersionList;
}

void from_json(const nlohmann::json& j, LayoutListItem& v) {
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, description);
}

void to_json(nlohmann::json& j, const LayoutListItem& v) {
    j["algorithmCode"]  = v.algorithmCode;
    j["algorithmName"]  = v.algorithmName;
    j["supplier"]       = v.supplier;
    j["algorithmUsage"] = v.algorithmUsage;
    j["description"]    = v.description;
}

void from_json(const nlohmann::json& j, LayoutListResult& v) {
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const LayoutListResult& v) {
    j["list"] = v.list;
}

void from_json(const nlohmann::json& j, AtomicAction& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, actionName);
    JSON_OPT(j, v, inputParamConfig);
    JSON_OPT(j, v, actionUsage);
    JSON_OPT(j, v, actionType);
}

void to_json(nlohmann::json& j, const AtomicAction& v) {
    j["id"]               = v.id;
    j["name"]             = v.name;
    j["actionName"]       = v.actionName;
    j["inputParamConfig"] = v.inputParamConfig;
    j["actionUsage"]      = v.actionUsage;
    j["actionType"]       = v.actionType;
}

void from_json(const nlohmann::json& j, AtomicActionListResult& v) {
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const AtomicActionListResult& v) {
    j["list"] = v.list;
}

}  // namespace cosmo::service::algorithm
