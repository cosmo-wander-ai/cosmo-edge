// SystemMaintainDto — Restart/Reset request

#include "SystemMaintainDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::System {
void to_json(nlohmann::json& j, const MsgResetSystemRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["resetOperation"] = v.resetOperation;
}

void from_json(const nlohmann::json& j, MsgResetSystemRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, resetOperation);
}

void to_json(nlohmann::json& j, const MsgResetSystemSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgResetSystemSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgExportFileRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["exportType"] = v.exportType;
}

void from_json(const nlohmann::json& j, MsgExportFileRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, exportType);
}

void to_json(nlohmann::json& j, const MsgExportFileSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgExportFileSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgUpgradeRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["contentLength"] = v.contentLength;
    j["fileName"]      = v.fileName;
    j["filePath"]      = v.filePath;
    j["fileUrl"]       = v.fileUrl;
}

void from_json(const nlohmann::json& j, MsgUpgradeRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, fileUrl);
}

void to_json(nlohmann::json& j, const MsgQueryDocumentUrlRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["type"] = v.type;
}

void from_json(const nlohmann::json& j, MsgQueryDocumentUrlRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, type);
}

void to_json(nlohmann::json& j, const MsgQueryDocumentUrlSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryDocumentUrlSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgResetSystemSend::ResData& v) {
    JSON_OPT(j, v, waitSeconds);
}

void to_json(nlohmann::json& j, const MsgResetSystemSend::ResData& v) {
    j["waitSeconds"] = v.waitSeconds;
}

void from_json(const nlohmann::json& j, MsgExportFileSend::ResData& v) {
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, fileUrl);
}

void to_json(nlohmann::json& j, const MsgExportFileSend::ResData& v) {
    j["fileName"] = v.fileName;
    j["fileUrl"]  = v.fileUrl;
}

void from_json(const nlohmann::json& j, MsgQueryDocumentUrlSend::ResData& v) {
    JSON_OPT(j, v, url);
}

void to_json(nlohmann::json& j, const MsgQueryDocumentUrlSend::ResData& v) {
    j["url"] = v.url;
}

}  // namespace cosmo::System
