// ImportFileDto — ImportFile DTO definitions (extracted from MessageImportFileHandler.h)

#include "service/path/dto/ImportFileDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::service {
void to_json(nlohmann::json& j, const MsgImportFileSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgImportFileSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgImportFileRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["importType"]    = v.importType;
    j["faceLibId"]     = v.faceLibId;
    j["contentLength"] = v.contentLength;
    j["filePath"]      = v.filePath;
    j["FileName"]      = v.filename;
}

void from_json(const nlohmann::json& j, MsgImportFileRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    if (j.contains("importType") && !j["importType"].is_null()) {
        auto& jt = j.at("importType");
        if (jt.is_number()) {
            jt.get_to(v.importType);
        } else if (jt.is_string()) {
            // Multipart form fields arrive as strings — convert safely
            auto s = jt.get<std::string>();
            try {
                v.importType = std::stoi(s);
            } catch (...) {
                v.importType = -1;
            }
        }
    }
    JSON_OPT(j, v, faceLibId);
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, filePath);
    JSON_OPT_KEY(j, v, "FileName", filename);
}

void to_json(nlohmann::json& j, const MsgQueryImportStatusRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["importType"] = v.importType;
    j["importId"]   = v.importId;
}

void from_json(const nlohmann::json& j, MsgQueryImportStatusRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("importType").get_to(v.importType);
    JSON_OPT(j, v, importId);
}

void to_json(nlohmann::json& j, const MsgQueryImportStatusSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryImportStatusSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgImportFileSend::ResData& v) {
    JSON_OPT(j, v, importId);
}

void to_json(nlohmann::json& j, const MsgImportFileSend::ResData& v) {
    j["importId"] = v.importId;
}

void from_json(const nlohmann::json& j, MsgQueryImportStatusSend::ResData& v) {
    JSON_OPT(j, v, importStatus);
}

void to_json(nlohmann::json& j, const MsgQueryImportStatusSend::ResData& v) {
    j["importStatus"] = v.importStatus;
}

void to_json(nlohmann::json& j, const MsgQueryImportStatusSend::ResData::ImportStatus& s) {
    j["status"]          = s.status;
    j["statusMsg"]       = s.statusMsg;
    j["progress"]        = s.progress;
    j["processedNumber"] = s.processedNumber;
    j["totalNumber"]     = s.totalNumber;
    if (s.importType == 2) {
        j["failedUrl"]     = s.failedUrl;
        j["successNumber"] = s.successNumber;
        j["failedNumber"]  = s.failedNumber;
    }
}

void from_json(const nlohmann::json& j, MsgQueryImportStatusSend::ResData::ImportStatus& s) {
    JSON_OPT(j, s, status);
    JSON_OPT(j, s, statusMsg);
    JSON_OPT(j, s, progress);
    JSON_OPT(j, s, processedNumber);
    JSON_OPT(j, s, totalNumber);
    JSON_OPT(j, s, failedUrl);
    JSON_OPT(j, s, successNumber);
    JSON_OPT(j, s, failedNumber);
    JSON_OPT(j, s, importType);
}

}  // namespace cosmo::service
