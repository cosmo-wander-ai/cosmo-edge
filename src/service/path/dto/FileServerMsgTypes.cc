// FileServerMsgTypes — File-server message types for upload/download URL management.

#include "service/path/dto/FileServerMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const FMsgSendHead& v) {
    j["resCode"] = v.resCode;
    j["resMsg"]  = v.resMsg;
}

void from_json(const nlohmann::json& j, FMsgSendHead& v) {
    j.at("resCode").get_to(v.resCode);
    JSON_OPT(j, v, resMsg);
}

void to_json(nlohmann::json& j, const FMsgReqGetFileUrl& v) {
    to_json(j, static_cast<const FMsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, FMsgReqGetFileUrl& v) {
    from_json(j, static_cast<FMsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const FMsgReqPUpFile& v) {
    to_json(j, static_cast<const FMsgSendHead&>(v));
    j["fileUrl"]    = v.fileUrl;
    j["uploadSize"] = v.uploadSize;
}

void from_json(const nlohmann::json& j, FMsgReqPUpFile& v) {
    from_json(j, static_cast<FMsgSendHead&>(v));
    JSON_OPT(j, v, fileUrl);
    JSON_OPT(j, v, uploadSize);
}

void from_json(const nlohmann::json& j, FMsgRspGetFileUrl& v) {
    JSON_OPT(j, v, isHttps);
    JSON_OPT(j, v, suffix);
}

void to_json(nlohmann::json& j, const FMsgRspGetFileUrl& v) {
    j["isHttps"] = v.isHttps;
    j["suffix"]  = v.suffix;
}

void from_json(const nlohmann::json& j, FMsgRspUpFile& v) {
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, down);
}

void to_json(nlohmann::json& j, const FMsgRspUpFile& v) {
    j["fileName"] = v.fileName;
    j["down"]     = v.down;
}

void from_json(const nlohmann::json& j, FMsgReqGetFileUrl::ResData& v) {
    JSON_OPT(j, v, fileUrl);
}

void to_json(nlohmann::json& j, const FMsgReqGetFileUrl::ResData& v) {
    j["fileUrl"] = v.fileUrl;
}

}  // namespace cosmo
