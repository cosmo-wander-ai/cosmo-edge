// AlgorithmDto_Layout — Algorithm Dto_ Layout implementation.

#include <nlohmann/json.hpp>

#include "AlgorithmDto.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Layout orchestration serialization (split from AlgorithmDto.cc)
namespace cosmo::Algorithm {

void to_json(nlohmann::json& j, const MsgLayoutSaveRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["confVersionId"]        = v.confVersionId;
    j["algorithmId"]          = v.algorithmId;
    j["configVersionName"]    = v.configVersionName;
    j["algorithmCategory"]    = v.algorithmCategory;
    j["algorithmUsage"]       = v.algorithmUsage;
    j["remark"]               = v.remark;
    j["atomicList"]           = v.atomicList;
    j["algorithmProcessdata"] = v.algorithmProcessdata;
    j["algorithmMetadata"]    = v.algorithmMetadata;
    j["filePath"]             = v.filePath;
}

void from_json(const nlohmann::json& j, MsgLayoutSaveRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("confVersionId").get_to(v.confVersionId);
    j.at("algorithmId").get_to(v.algorithmId);
    JSON_OPT(j, v, configVersionName);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, remark);
    JSON_OPT(j, v, atomicList);
    JSON_OPT(j, v, algorithmProcessdata);
    JSON_OPT(j, v, algorithmMetadata);
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgLayoutDetailRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["id"]       = v.id;
    j["filePath"] = v.filePath;
}

void from_json(const nlohmann::json& j, MsgLayoutDetailRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("id").get_to(v.id);
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgLayoutDetailSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgLayoutDetailSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgLayoutListRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["supplier"]       = v.supplier;
    j["algorithmUsage"] = v.algorithmUsage;
    j["filePath"]       = v.filePath;
}

void from_json(const nlohmann::json& j, MsgLayoutListRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, filePath);
}

void to_json(nlohmann::json& j, const MsgLayoutListSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgLayoutListSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgLayoutExportSingleRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmCode"]     = v.algorithmCode;
    j["algorithmName"]     = v.algorithmName;
    j["algorithmCategory"] = v.algorithmCategory;
    j["algorithmUsage"]    = v.algorithmUsage;
    j["supplier"]          = v.supplier;
    j["confVersionId"]     = v.confVersionId;
}

void from_json(const nlohmann::json& j, MsgLayoutExportSingleRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, confVersionId);
}

void to_json(nlohmann::json& j, const MsgLayoutExportSingleSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["filePath"] = v.filePath;
    j["fileName"] = v.fileName;
}

void from_json(const nlohmann::json& j, MsgLayoutExportSingleSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, fileName);
}

void to_json(nlohmann::json& j, const MsgLayoutExportAllRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["algorithmCode"]     = v.algorithmCode;
    j["algorithmName"]     = v.algorithmName;
    j["algorithmCategory"] = v.algorithmCategory;
    j["algorithmUsage"]    = v.algorithmUsage;
    j["supplier"]          = v.supplier;
    j["algorithmIds"]      = v.algorithmIds;
}

void from_json(const nlohmann::json& j, MsgLayoutExportAllRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, algorithmCategory);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, algorithmIds);
}

void to_json(nlohmann::json& j, const MsgLayoutExportAllSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["filePath"] = v.filePath;
    j["fileName"] = v.fileName;
}

void from_json(const nlohmann::json& j, MsgLayoutExportAllSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, fileName);
}

void from_json(const nlohmann::json& j, MsgLayoutDetailVersion& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmMetadata);
    JSON_OPT(j, v, algorithmProcessdata);
    JSON_OPT(j, v, atomicList);
    JSON_OPT(j, v, algorithmUpdateTime);
}

void to_json(nlohmann::json& j, const MsgLayoutDetailVersion& v) {
    j["id"]                   = v.id;
    j["name"]                 = v.name;
    j["algorithmCode"]        = v.algorithmCode;
    j["algorithmMetadata"]    = v.algorithmMetadata;
    j["algorithmProcessdata"] = v.algorithmProcessdata;
    j["atomicList"]           = v.atomicList;
    j["algorithmUpdateTime"]  = v.algorithmUpdateTime;
}

void from_json(const nlohmann::json& j, MsgLayoutDetailSend::ResData& v) {
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

void to_json(nlohmann::json& j, const MsgLayoutDetailSend::ResData& v) {
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

void from_json(const nlohmann::json& j, MsgLayoutListItem& v) {
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, supplier);
    JSON_OPT(j, v, algorithmUsage);
    JSON_OPT(j, v, description);
}

void to_json(nlohmann::json& j, const MsgLayoutListItem& v) {
    j["algorithmCode"]  = v.algorithmCode;
    j["algorithmName"]  = v.algorithmName;
    j["supplier"]       = v.supplier;
    j["algorithmUsage"] = v.algorithmUsage;
    j["description"]    = v.description;
}

void from_json(const nlohmann::json& j, MsgLayoutListSend::ResData& v) {
    JSON_OPT(j, v, list);
}

void to_json(nlohmann::json& j, const MsgLayoutListSend::ResData& v) {
    j["list"] = v.list;
}

}  // namespace cosmo::Algorithm
