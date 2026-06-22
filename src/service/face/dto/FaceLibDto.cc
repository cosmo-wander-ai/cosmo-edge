// FaceLibDto — FaceLib DTO definitions (extracted from MessageFaceLibHandler.h)

#include "FaceLibDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::Lib {
void to_json(nlohmann::json& j, const MsgModifyFaceLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgModifyFaceLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyFaceLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["faceLibOperation"] = v.faceLibOperation;
    j["faceLib"]          = v.faceLib;
}

void from_json(const nlohmann::json& j, MsgModifyFaceLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("faceLibOperation").get_to(v.faceLibOperation);
    j.at("faceLib").get_to(v.faceLib);
}

void to_json(nlohmann::json& j, const MsgModifyFacePicLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgModifyFacePicLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgModifyFacePicLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgConditionLib&>(v));
}

void from_json(const nlohmann::json& j, MsgModifyFacePicLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgConditionLib&>(v));
}

void to_json(nlohmann::json& j, const MsgModifyTaskAreaSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgModifyTaskAreaSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    to_json(j, static_cast<const MsgQueryFaceLibInfoS&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    from_json(j, static_cast<MsgQueryFaceLibInfoS&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgQueryFaceLibInfoR&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgQueryFaceLibInfoR&>(v));
}

void to_json(nlohmann::json& j, const MsgDeleteFaceLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDeleteFaceLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteFaceLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["faceLibIdList"] = v.faceLibIdList;
}

void from_json(const nlohmann::json& j, MsgDeleteFaceLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("faceLibIdList").get_to(v.faceLibIdList);
}

void to_json(nlohmann::json& j, const MsgQueryFacesRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgQueryFacesR&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryFacesRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgQueryFacesR&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryFacesSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    to_json(j, static_cast<const MsgQueryFacesS&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryFacesSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    from_json(j, static_cast<MsgQueryFacesS&>(v));
}

void to_json(nlohmann::json& j, const MsgDeletePersonRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["removeAll"]    = v.removeAll;
    j["faceLibId"]    = v.faceLibId;
    j["personIdList"] = v.personIdList;
}

void from_json(const nlohmann::json& j, MsgDeletePersonRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, removeAll);
    JSON_OPT(j, v, faceLibId);
    JSON_OPT(j, v, personIdList);
}

void to_json(nlohmann::json& j, const MsgDeletePersonSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["failedList"] = v.failedList;
}

void from_json(const nlohmann::json& j, MsgDeletePersonSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, failedList);
}

void from_json(const nlohmann::json& j, MsgModifyFaceLibSend::ResData& v) {
    JSON_OPT(j, v, faceLibId);
}

void to_json(nlohmann::json& j, const MsgModifyFaceLibSend::ResData& v) {
    j["faceLibId"] = v.faceLibId;
}

void from_json(const nlohmann::json& j, MsgModifyFacePicLibSend::ResData& v) {
    JSON_OPT(j, v, personId);
}

void to_json(nlohmann::json& j, const MsgModifyFacePicLibSend::ResData& v) {
    j["personId"] = v.personId;
}

void from_json(const nlohmann::json& j, MsgModifyTaskAreaSend::ResData& v) {
    JSON_OPT(j, v, areaList);
    JSON_OPT(j, v, invalidAreaList);
}

void to_json(nlohmann::json& j, const MsgModifyTaskAreaSend::ResData& v) {
    j["areaList"]        = v.areaList;
    j["invalidAreaList"] = v.invalidAreaList;
}

void from_json(const nlohmann::json& j, MsgDeleteFaceLibSend::ResData& v) {
    JSON_OPT(j, v, failedFaceLibList);
}

void to_json(nlohmann::json& j, const MsgDeleteFaceLibSend::ResData& v) {
    j["failedFaceLibList"] = v.failedFaceLibList;
}

}  // namespace cosmo::Lib
