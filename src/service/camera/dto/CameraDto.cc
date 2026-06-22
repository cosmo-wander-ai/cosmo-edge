// CameraDto — Camera DTO definitions (extracted from MessageCameraHandler.h)

#include "service/camera/dto/CameraDto.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::camera {
void to_json(nlohmann::json& j, const MsgAddRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgCameraInfo&>(v));
}

void from_json(const nlohmann::json& j, MsgAddRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgCameraInfo&>(v));
}

void to_json(nlohmann::json& j, const MsgAddSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgAddSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgAddVideoRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["contentLength"] = v.contentLength;
    j["fileName"]      = v.fileName;
    j["filePath"]      = v.filePath;
    j["channelName"]   = v.channelName;
}

void from_json(const nlohmann::json& j, MsgAddVideoRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    JSON_OPT(j, v, contentLength);
    JSON_OPT(j, v, fileName);
    JSON_OPT(j, v, filePath);
    JSON_OPT(j, v, channelName);
}

void to_json(nlohmann::json& j, const MsgAddVideoSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgAddVideoSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgUpdateRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgCameraInfo&>(v));
}

void from_json(const nlohmann::json& j, MsgUpdateRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgCameraInfo&>(v));
}

void to_json(nlohmann::json& j, const MsgPageRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    to_json(j, static_cast<const MsgConditionPage&>(v));
    j["channelName"]   = v.channelName;
    j["channelStatus"] = v.channelStatus;
}

void from_json(const nlohmann::json& j, MsgPageRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    from_json(j, static_cast<MsgConditionPage&>(v));
    JSON_OPT(j, v, channelName);
    JSON_OPT(j, v, channelStatus);
}

void to_json(nlohmann::json& j, const MsgPageSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgPageSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["videoChannelId"] = v.videoChannelId;
}

void from_json(const nlohmann::json& j, MsgDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("videoChannelId").get_to(v.videoChannelId);
}

void to_json(nlohmann::json& j, const MsgBatchDeleteRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["videoChannelIds"] = v.videoChannelIds;
}

void from_json(const nlohmann::json& j, MsgBatchDeleteRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("videoChannelIds").get_to(v.videoChannelIds);
}

void to_json(nlohmann::json& j, const MsgBatchDeleteSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgBatchDeleteSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgGetPictureRecv& v) {
    to_json(j, static_cast<const MsgRecvHead&>(v));
    j["videoChannelId"] = v.videoChannelId;
}

void from_json(const nlohmann::json& j, MsgGetPictureRecv& v) {
    from_json(j, static_cast<MsgRecvHead&>(v));
    j.at("videoChannelId").get_to(v.videoChannelId);
}

void to_json(nlohmann::json& j, const MsgGetPictureSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgGetPictureSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryUsbCameraListSend& v) {
    to_json(j, static_cast<const MsgSendHead&>(v));
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryUsbCameraListSend& v) {
    from_json(j, static_cast<MsgSendHead&>(v));
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgUsbCameraDevice& v) {
    JSON_OPT(j, v, usbDeviceIndex);
    JSON_OPT(j, v, devicePath);
}

void to_json(nlohmann::json& j, const MsgUsbCameraDevice& v) {
    j["usbDeviceIndex"] = v.usbDeviceIndex;
    j["devicePath"]     = v.devicePath;
}

void from_json(const nlohmann::json& j, MsgAddSend::ResData& v) {
    JSON_OPT(j, v, id);
}

void to_json(nlohmann::json& j, const MsgAddSend::ResData& v) {
    j["id"] = v.id;
}

void from_json(const nlohmann::json& j, MsgAddVideoSend::ResData& v) {
    JSON_OPT(j, v, id);
}

void to_json(nlohmann::json& j, const MsgAddVideoSend::ResData& v) {
    j["id"] = v.id;
}

void from_json(const nlohmann::json& j, MsgPageSend::ResData& v) {
    JSON_OPT(j, v, total);
    JSON_OPT(j, v, rows);
}

void to_json(nlohmann::json& j, const MsgPageSend::ResData& v) {
    j["total"] = v.total;
    j["rows"]  = v.rows;
}

void from_json(const nlohmann::json& j, MsgBatchDeleteSend::ResData& v) {
    JSON_OPT(j, v, failedList);
}

void to_json(nlohmann::json& j, const MsgBatchDeleteSend::ResData& v) {
    j["failedList"] = v.failedList;
}

void from_json(const nlohmann::json& j, MsgGetPictureSend::ResData& v) {
    JSON_OPT(j, v, url);
}

void to_json(nlohmann::json& j, const MsgGetPictureSend::ResData& v) {
    j["url"] = v.url;
}

void from_json(const nlohmann::json& j, MsgQueryUsbCameraListSend::ResData& v) {
    JSON_OPT(j, v, rows);
}

void to_json(nlohmann::json& j, const MsgQueryUsbCameraListSend::ResData& v) {
    j["rows"] = v.rows;
}

}  // namespace cosmo::camera
