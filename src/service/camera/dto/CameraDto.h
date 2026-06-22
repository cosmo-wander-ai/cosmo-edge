// Camera DTO definitions (extracted from MessageCameraHandler.h)

#pragma once

#include "util/dto/CameraMsgTypes.h"
#include "util/dto/EventMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo::camera {
struct MsgUsbCameraDevice {
    int usbDeviceIndex{0};
    std::string devicePath;
    friend void to_json(nlohmann::json& j, const MsgUsbCameraDevice& v);
    friend void from_json(const nlohmann::json& j, MsgUsbCameraDevice& v);
};

struct MsgAddRecv : public MsgRecvHead, public MsgCameraInfo {};

void to_json(nlohmann::json& j, const MsgAddRecv& v);
void from_json(const nlohmann::json& j, MsgAddRecv& v);

// Add camera response.
struct MsgAddSend : public MsgSendHead {
    struct ResData {
        std::string id;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgAddSend& v);
void from_json(const nlohmann::json& j, MsgAddSend& v);

struct MsgAddVideoRecv : public MsgRecvHead {
    std::string contentLength;
    std::string fileName;
    std::string filePath;
    std::string channelCode;
    std::string channelName;
};

void to_json(nlohmann::json& j, const MsgAddVideoRecv& v);
void from_json(const nlohmann::json& j, MsgAddVideoRecv& v);

// Add video camera response.
struct MsgAddVideoSend : public MsgSendHead {
    struct ResData {
        std::string id;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgAddVideoSend& v);
void from_json(const nlohmann::json& j, MsgAddVideoSend& v);

struct MsgUpdateRecv : public MsgRecvHead, public MsgCameraInfo {};

void to_json(nlohmann::json& j, const MsgUpdateRecv& v);
void from_json(const nlohmann::json& j, MsgUpdateRecv& v);

// Update camera response.
struct MsgUpdateSend : public MsgSendHead {};

struct MsgPageRecv : public MsgRecvHead, public MsgConditionPage {
    std::string channelName;
    ChannelStatus channelStatus{ChannelStatus::ChannelStatusAll};
};

void to_json(nlohmann::json& j, const MsgPageRecv& v);
void from_json(const nlohmann::json& j, MsgPageRecv& v);

// Paginated camera query response.
struct MsgPageSend : public MsgSendHead {
    struct ResData {
        size_t total{0};
        std::vector<MsgCameraInfo> rows;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgPageSend& v);
void from_json(const nlohmann::json& j, MsgPageSend& v);

struct MsgDeleteRecv : public MsgRecvHead {
    std::string videoChannelId;
};

void to_json(nlohmann::json& j, const MsgDeleteRecv& v);
void from_json(const nlohmann::json& j, MsgDeleteRecv& v);

// Delete camera response.
struct MsgDeleteSend : public MsgSendHead {};

struct MsgBatchDeleteRecv : public MsgRecvHead {
    std::vector<std::string> videoChannelIds;
};

void to_json(nlohmann::json& j, const MsgBatchDeleteRecv& v);
void from_json(const nlohmann::json& j, MsgBatchDeleteRecv& v);

// Batch delete camera response.
struct MsgBatchDeleteSend : public MsgSendHead {
    struct ResData {
        std::vector<MsgResultInfo> failedList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgBatchDeleteSend& v);
void from_json(const nlohmann::json& j, MsgBatchDeleteSend& v);

struct MsgGetPictureRecv : public MsgRecvHead {
    std::string videoChannelId;
};

void to_json(nlohmann::json& j, const MsgGetPictureRecv& v);
void from_json(const nlohmann::json& j, MsgGetPictureRecv& v);

// Get picture response.
struct MsgGetPictureSend : public MsgSendHead {
    struct ResData {
        std::string url;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgGetPictureSend& v);
void from_json(const nlohmann::json& j, MsgGetPictureSend& v);

struct MsgQueryUsbCameraListRecv : public MsgRecvHead {};

struct MsgQueryUsbCameraListSend : public MsgSendHead {
    struct ResData {
        std::vector<MsgUsbCameraDevice> rows;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryUsbCameraListSend& v);
void from_json(const nlohmann::json& j, MsgQueryUsbCameraListSend& v);
}  // namespace cosmo::camera
