// AlgorithmDto — Algorithm DTO definitions (extracted from MessageAlgorithmHandler.h)

#include "AlgorithmDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Algorithm CRUD serialization (Layout in AlgorithmDto_Layout.cc, Action in AlgorithmDto_Action.cc)
namespace cosmo::Algorithm {
void to_json(nlohmann::json& j, const MsgPageRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmUsage"]    = v.algorithmUsage;
    j["algorithmName"]     = v.algorithmName;
    j["supplier"]          = v.supplier;
    j["algorithmId"]       = v.algorithmId;
    j["algorithmCategory"] = v.algorithmCategory;
    j["pageNum"]           = v.pageNum;
    j["pageSize"]          = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgPageRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

void to_json(nlohmann::json& j, const MsgAlgorithmModelStatus& v) {
    j["modelCode"]   = v.modelCode;
    j["modelName"]   = v.modelName;
    j["modelStatus"] = v.modelStatus;
}

void from_json(const nlohmann::json& j, MsgAlgorithmModelStatus& v) {
    j.at("modelCode").get_to(v.modelCode);
    j.at("modelName").get_to(v.modelName);
    j.at("modelStatus").get_to(v.modelStatus);
}

void to_json(nlohmann::json& j, const MsgPageSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgPageSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgUploadRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["contentLength"] = v.contentLength;
    j["fileName"]      = v.fileName;
    j["filePath"]      = v.filePath;
}

void from_json(const nlohmann::json& j, MsgUploadRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgUpdateRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmId"]       = v.algorithmId;
    j["algorithmName"]     = v.algorithmName;
    j["algorithmCategory"] = v.algorithmCategory;
    j["remark"]            = v.remark;
}

void from_json(const nlohmann::json& j, MsgUpdateRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("algorithmId").get_to(v.algorithmId);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, remark);
}

void to_json(nlohmann::json& j, const MsgDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmId"] = v.algorithmId;
}

void from_json(const nlohmann::json& j, MsgDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("algorithmId").get_to(v.algorithmId);
}

void to_json(nlohmann::json& j, const MsgAddRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmCode"]     = v.algorithmCode;
    j["algorithmName"]     = v.algorithmName;
    j["algorithmCategory"] = v.algorithmCategory;
    j["algorithmUsage"]    = v.algorithmUsage;
    j["checkType"]         = v.checkType;
    j["remark"]            = v.remark;
    j["eventType"]         = v.eventType;
    j["filePath"]          = v.filePath;
}

void from_json(const nlohmann::json& j, MsgAddRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, checkType);
    JSON_OPT(j, v, remark);
    JSON_OPT(j, v, eventType);
    JSON_OPT(j, v, filePath);
}

void from_json(const nlohmann::json& j, MsgAlgorithmVersions& v) {
    JSON_OPT(j, v, gpuCode);
    JSON_OPT(j, v, versionNumber);
    JSON_OPT(j, v, status);
}

void to_json(nlohmann::json& j, const MsgAlgorithmVersions& v) {
    j["gpuCode"]       = v.gpuCode;
    j["versionNumber"] = v.versionNumber;
    j["status"]        = v.status;
}

void from_json(const nlohmann::json& j, MsgAlgorithm& v) {
    JSON_OPT(j, v, algorithmId);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, categoryName);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, algorithmMetadata);
    JSON_OPT(j, v, remark);
    JSON_OPT(j, v, configType);
    JSON_OPT(j, v, releaseTime);
    JSON_OPT(j, v, confVersionName);
    JSON_OPT(j, v, versionCount);
    JSON_OPT(j, v, authStatus);
    JSON_OPT(j, v, runningStatus);
    JSON_OPT(j, v, versions);
    JSON_OPT(j, v, models);
}

void to_json(nlohmann::json& j, const MsgAlgorithm& v) {
    j["algorithmId"]       = v.algorithmId;
    j["algorithmName"]     = v.algorithmName;
    j["algorithmCategory"] = v.algorithmCategory;
    j["algorithmUsage"]    = v.algorithmUsage;
    j["categoryName"]      = v.categoryName;
    j["supplier"]          = v.supplier;
    j["algorithmMetadata"] = v.algorithmMetadata;
    j["remark"]            = v.remark;
    j["configType"]        = v.configType;
    j["releaseTime"]       = v.releaseTime;
    j["confVersionName"]   = v.confVersionName;
    j["versionCount"]      = v.versionCount;
    j["authStatus"]        = v.authStatus;
    j["runningStatus"]     = v.runningStatus;
    j["versions"]          = v.versions;
    j["models"]            = v.models;
}

void from_json(const nlohmann::json& j, MsgPageSend::ResData& v) {
    JSON_OPT(j, v, total);
    JSON_OPT(j, v, rows);
}

void to_json(nlohmann::json& j, const MsgPageSend::ResData& v) {
    j["total"] = v.total;
    j["rows"]  = v.rows;
}

}  // namespace cosmo::Algorithm
