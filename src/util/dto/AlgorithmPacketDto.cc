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

}  // namespace cosmo::service::algorithm
