// BodyLibDto — BodyLib DTO definitions (extracted from MessageBodyLibHandler.h)

#include "BodyLibDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::BodyLib {
void to_json(nlohmann::json& j, const MsgModifyPersonLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["personLibOperation"] = v.personLibOperation;
    j["personLib"]          = v.personLib;
}

void from_json(const nlohmann::json& j, MsgModifyPersonLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("personLibOperation").get_to(v.personLibOperation);
    j.at("personLib").get_to(v.personLib);
}

void to_json(nlohmann::json& j, const MsgModifyPersonLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgModifyPersonLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgAddLibPersonRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["personOperation"] = v.personOperation;
    j["personLibId"]     = v.personLibId;
    j["personList"]      = v.personList;
}

void from_json(const nlohmann::json& j, MsgAddLibPersonRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("personOperation").get_to(v.personOperation);
    j.at("personLibId").get_to(v.personLibId);
    j.at("personList").get_to(v.personList);
}

void to_json(nlohmann::json& j, const MsgDeletePersonLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["personLibIdList"] = v.personLibIdList;
}

void from_json(const nlohmann::json& j, MsgDeletePersonLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("personLibIdList").get_to(v.personLibIdList);
}

void to_json(nlohmann::json& j, const MsgDeletePersonLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDeletePersonLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgAddLibPersonSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgAddLibPersonSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgQueryPersonInfoRecv&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgQueryPersonInfoRecv&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    to_json(j, static_cast<const MsgQueryPersonLibInfoS&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    from_json(j, static_cast<MsgQueryPersonLibInfoS&>(v));
}

void to_json(nlohmann::json& j, const MsgBindTaskPersonLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgBindTaskPersonLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgBindTaskPersonLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgBaseCameraTask&>(v));
    j["personLibId"] = v.personLibId;
    j["searchAll"]   = v.searchAll;
}

void from_json(const nlohmann::json& j, MsgBindTaskPersonLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgBaseCameraTask&>(v));
    j.at("personLibId").get_to(v.personLibId);
    JSON_OPT(j, v, searchAll);
}

void to_json(nlohmann::json& j, const MsgDeleteLibPersonRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["removeAll"]    = v.removeAll;
    j["personLibId"]  = v.personLibId;
    j["personIdList"] = v.personIdList;
}

void from_json(const nlohmann::json& j, MsgDeleteLibPersonRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, removeAll);
    JSON_OPT(j, v, personLibId);
    JSON_OPT(j, v, personIdList);
}

void to_json(nlohmann::json& j, const MsgDeleteLibPersonSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["failedList"] = v.failedList;
}

void from_json(const nlohmann::json& j, MsgDeleteLibPersonSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, failedList);
}

void to_json(nlohmann::json& j, const MsgDetectPersonRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["imageBase64"] = v.imageBase64;
}

void from_json(const nlohmann::json& j, MsgDetectPersonRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, imageBase64);
}

void to_json(nlohmann::json& j, const MsgDetectPersonSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDetectPersonSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgGetPersonPictureRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["cameraId"] = v.cameraId;
}

void from_json(const nlohmann::json& j, MsgGetPersonPictureRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("cameraId").get_to(v.cameraId);
}

void to_json(nlohmann::json& j, const MsgGetPersonPictureSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgGetPersonPictureSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgQueryPersonPicturesR&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryPersonPicturesRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgQueryPersonPicturesR&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    to_json(j, static_cast<const MsgQueryPersonPicturesS&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryPersonPicturesSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    from_json(j, static_cast<MsgQueryPersonPicturesS&>(v));
}

void from_json(const nlohmann::json& j, MsgModifyPersonLibSend::ResData& v) {
    JSON_OPT(j, v, personLibId);
}

void to_json(nlohmann::json& j, const MsgModifyPersonLibSend::ResData& v) {
    j["personLibId"] = v.personLibId;
}

void from_json(const nlohmann::json& j, MsgDeletePersonLibSend::ResData& v) {
    JSON_OPT(j, v, failedPersonLibList);
}

void to_json(nlohmann::json& j, const MsgDeletePersonLibSend::ResData& v) {
    j["failedPersonLibList"] = v.failedPersonLibList;
}

void from_json(const nlohmann::json& j, MsgAddLibPersonSend::ResData& v) {
    JSON_OPT(j, v, personId);
}

void to_json(nlohmann::json& j, const MsgAddLibPersonSend::ResData& v) {
    j["personId"] = v.personId;
}

void from_json(const nlohmann::json& j, MsgBindTaskPersonLibSend::ResData& v) {
    JSON_OPT(j, v, failedPersonLibList);
}

void to_json(nlohmann::json& j, const MsgBindTaskPersonLibSend::ResData& v) {
    j["failedPersonLibList"] = v.failedPersonLibList;
}

void from_json(const nlohmann::json& j, MsgDetectPersonSend::ResData& v) {
    JSON_OPT(j, v, pictureUrl);
}

void to_json(nlohmann::json& j, const MsgDetectPersonSend::ResData& v) {
    j["pictureUrl"] = v.pictureUrl;
}

void from_json(const nlohmann::json& j, MsgGetPersonPictureSend::ResData& v) {
    JSON_OPT(j, v, pictureUrl);
}

void to_json(nlohmann::json& j, const MsgGetPersonPictureSend::ResData& v) {
    j["pictureUrl"] = v.pictureUrl;
}

void to_json(nlohmann::json& j, const MsgAddLibPersonRecv::Person& v) {
    j["pictureUrl"]  = v.pictureUrl;
    j["pictureName"] = v.pictureName;
}

void from_json(const nlohmann::json& j, MsgAddLibPersonRecv::Person& v) {
    JSON_OPT(j, v, pictureUrl);
    JSON_OPT(j, v, pictureName);
}

}  // namespace cosmo::BodyLib
