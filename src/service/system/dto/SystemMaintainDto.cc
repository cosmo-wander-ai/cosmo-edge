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
    j["uploadId"]      = v.uploadId;
    j["fileUrl"]       = v.fileUrl;
}

void from_json(const nlohmann::json& j, MsgUpgradeRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, uploadId);
    JSON_OPT(j, v, fileUrl);
}

void to_json(nlohmann::json& j, const MsgCheckUpgradeSpaceRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["packageSizeBytes"]  = v.packageSizeBytes;
    j["cleanupEventMedia"] = v.cleanupEventMedia;
}

void from_json(const nlohmann::json& j, MsgCheckUpgradeSpaceRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, packageSizeBytes);
    JSON_OPT(j, v, cleanupEventMedia);
}

void to_json(nlohmann::json& j, const MsgCheckUpgradeSpaceSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgCheckUpgradeSpaceSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgCheckUpgradeSpaceSend::ResData& v) {
    j = nlohmann::json{{"sufficient", v.sufficient},
                       {"requiredBytes", v.requiredBytes},
                       {"availableBytes", v.availableBytes},
                       {"eventMediaBytes", v.eventMediaBytes},
                       {"deletedMediaBytes", v.deletedMediaBytes},
                       {"deletedMediaFiles", v.deletedMediaFiles}};
}

void from_json(const nlohmann::json& j, MsgCheckUpgradeSpaceSend::ResData& v) {
    JSON_OPT(j, v, sufficient);
    JSON_OPT(j, v, requiredBytes);
    JSON_OPT(j, v, availableBytes);
    JSON_OPT(j, v, eventMediaBytes);
    JSON_OPT(j, v, deletedMediaBytes);
    JSON_OPT(j, v, deletedMediaFiles);
}

void to_json(nlohmann::json& j, const MsgQueryModelAuthorizationSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}
void from_json(const nlohmann::json& j, MsgQueryModelAuthorizationSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}
void to_json(nlohmann::json& j, const MsgQueryModelAuthorizationSend::ResData& v) {
    j = nlohmann::json{{"supported", v.supported}, {"authorized", v.authorized}, {"state", v.state}};
}
void from_json(const nlohmann::json& j, MsgQueryModelAuthorizationSend::ResData& v) {
    if (j.contains("supported"))
        j.at("supported").get_to(v.supported);
    if (j.contains("authorized"))
        j.at("authorized").get_to(v.authorized);
    if (j.contains("state"))
        j.at("state").get_to(v.state);
}
void to_json(nlohmann::json& j, const MsgDownloadModelAuthorizationRequestSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["filePath"] = v.filePath;
    j["fileName"] = v.fileName;
}
void from_json(const nlohmann::json& j, MsgDownloadModelAuthorizationRequestSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, fileName);
}
void to_json(nlohmann::json& j, const MsgDownloadLogsSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["filePath"] = v.filePath;
    j["fileName"] = v.fileName;
}
void from_json(const nlohmann::json& j, MsgDownloadLogsSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, fileName);
}
void to_json(nlohmann::json& j, const MsgInstallModelAuthorizationRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["uploadId"] = v.uploadId;
    j["filePath"] = v.filePath;
}
void from_json(const nlohmann::json& j, MsgInstallModelAuthorizationRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, uploadId);
    JSON_OPT(j, v, filePath);
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
