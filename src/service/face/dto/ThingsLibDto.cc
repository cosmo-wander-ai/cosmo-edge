// ThingsLibDto — ThingsLib DTO definitions (extracted from MessageThingsLibHandler.h)

#include "ThingsLibDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::ThingsLib {
void to_json(nlohmann::json& j, const MsgModifyThingsLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["thingsLibOperation"] = v.thingsLibOperation;
    j["thingsLib"]          = v.thingsLib;
}

void from_json(const nlohmann::json& j, MsgModifyThingsLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("thingsLibOperation").get_to(v.thingsLibOperation);
    j.at("thingsLib").get_to(v.thingsLib);
}

void to_json(nlohmann::json& j, const MsgModifyThingsLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgModifyThingsLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteThingsLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["thingsLibIdList"] = v.thingsLibIdList;
}

void from_json(const nlohmann::json& j, MsgDeleteThingsLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("thingsLibIdList").get_to(v.thingsLibIdList);
}

void to_json(nlohmann::json& j, const MsgDeleteThingsLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgDeleteThingsLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgQueryThingsLibInfoR&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgQueryThingsLibInfoR&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    to_json(j, static_cast<const MsgQueryThingsLibInfoS&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    from_json(j, static_cast<MsgQueryThingsLibInfoS&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgQueryThingsPicturesR&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryThingsPicturesRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgQueryThingsPicturesR&>(v));
}

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    to_json(j, static_cast<const MsgQueryThingsPicturesS&>(v));
}

void from_json(const nlohmann::json& j, MsgQueryThingsPicturesSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    from_json(j, static_cast<MsgQueryThingsPicturesS&>(v));
}

void to_json(nlohmann::json& j, const MsgGetThingsPictureRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["cameraId"] = v.cameraId;
    j["taskType"] = v.taskType;
    j["rect"]     = v.rect;
}

void from_json(const nlohmann::json& j, MsgGetThingsPictureRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("cameraId").get_to(v.cameraId);
    JSON_OPT(j, v, taskType);
    JSON_OPT(j, v, rect);
}

void to_json(nlohmann::json& j, const MsgGetThingsPictureSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgGetThingsPictureSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgAddLibThingsRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["thingsOperation"] = v.thingsOperation;
    j["thingsLibId"]     = v.thingsLibId;
    j["thingsList"]      = v.thingsList;
}

void from_json(const nlohmann::json& j, MsgAddLibThingsRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("thingsOperation").get_to(v.thingsOperation);
    j.at("thingsLibId").get_to(v.thingsLibId);
    j.at("thingsList").get_to(v.thingsList);
}

void to_json(nlohmann::json& j, const MsgAddLibThingsSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgAddLibThingsSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgBindTaskThingsLibRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgBaseCameraTask&>(v));
    j["thingsLibId"] = v.thingsLibId;
    j["searchAll"]   = v.searchAll;
}

void from_json(const nlohmann::json& j, MsgBindTaskThingsLibRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgBaseCameraTask&>(v));
    j.at("thingsLibId").get_to(v.thingsLibId);
    JSON_OPT(j, v, searchAll);
}

void to_json(nlohmann::json& j, const MsgBindTaskThingsLibSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgBindTaskThingsLibSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteLibThingsRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["removeAll"]    = v.removeAll;
    j["thingsLibId"]  = v.thingsLibId;
    j["thingsIdList"] = v.thingsIdList;
}

void from_json(const nlohmann::json& j, MsgDeleteLibThingsRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, removeAll);
    JSON_OPT(j, v, thingsLibId);
    JSON_OPT(j, v, thingsIdList);
}

void to_json(nlohmann::json& j, const MsgDeleteLibThingsSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["failedList"] = v.failedList;
}

void from_json(const nlohmann::json& j, MsgDeleteLibThingsSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, failedList);
}

void from_json(const nlohmann::json& j, MsgModifyThingsLibSend::ResData& v) {
    JSON_OPT(j, v, thingsLibId);
}

void to_json(nlohmann::json& j, const MsgModifyThingsLibSend::ResData& v) {
    j["thingsLibId"] = v.thingsLibId;
}

void from_json(const nlohmann::json& j, MsgDeleteThingsLibSend::ResData& v) {
    JSON_OPT(j, v, failedThingsLibList);
}

void to_json(nlohmann::json& j, const MsgDeleteThingsLibSend::ResData& v) {
    j["failedThingsLibList"] = v.failedThingsLibList;
}

void from_json(const nlohmann::json& j, MsgGetThingsPictureSend::ResData& v) {
    JSON_OPT(j, v, pictureUrl);
}

void to_json(nlohmann::json& j, const MsgGetThingsPictureSend::ResData& v) {
    j["pictureUrl"] = v.pictureUrl;
}

void from_json(const nlohmann::json& j, MsgAddLibThingsSend::ResData& v) {
    JSON_OPT(j, v, thingsId);
}

void to_json(nlohmann::json& j, const MsgAddLibThingsSend::ResData& v) {
    j["thingsId"] = v.thingsId;
}

void from_json(const nlohmann::json& j, MsgBindTaskThingsLibSend::ResData& v) {
    JSON_OPT(j, v, failedThingsLibList);
}

void to_json(nlohmann::json& j, const MsgBindTaskThingsLibSend::ResData& v) {
    j["failedThingsLibList"] = v.failedThingsLibList;
}

void to_json(nlohmann::json& j, const MsgTaskAreaRect& v) {
    j["x"] = v.x;
    j["y"] = v.y;
    j["w"] = v.w;
    j["h"] = v.h;
}

void from_json(const nlohmann::json& j, MsgTaskAreaRect& v) {
    JSON_OPT(j, v, x);
    JSON_OPT(j, v, y);
    JSON_OPT(j, v, w);
    JSON_OPT(j, v, h);
}

void to_json(nlohmann::json& j, const MsgAddLibThingsRecv::ArticlesReid& v) {
    j["pictureUrl"]    = v.pictureUrl;
    j["pictureName"]   = v.pictureName;
    j["pictureBase64"] = v.pictureBase64;
}

void from_json(const nlohmann::json& j, MsgAddLibThingsRecv::ArticlesReid& v) {
    JSON_OPT(j, v, pictureUrl);
    JSON_OPT(j, v, pictureName);
    JSON_OPT(j, v, pictureBase64);
}

}  // namespace cosmo::ThingsLib
