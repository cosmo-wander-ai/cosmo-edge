// Camera types — MsgCameraAttr, MsgCameraInfo, ChannelStatus, MsgBaseCameraTask.
// Modular DTO header.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct MsgBaseCameraTask {
    util::String<1, 36> cameraId;
    std::string algorithmCode;  // TaskTypeSize
};

void to_json(nlohmann::json& j, const MsgBaseCameraTask& v);
void from_json(const nlohmann::json& j, MsgBaseCameraTask& v);

enum class ChannelStatus {
    ChannelStatusAll = -1  // Used for query-all filter
    ,
    ChannelStatusOffline             = 0,
    ChannelStatusOnline              = 1,
    ChannelStatusUnauthorized        = 2,
    ChannelStatusResolusionUnSupport = 3
};

struct MsgCameraAttr {
    int width{0};
    int height{0};
    std::string codec;
    float fps{0.0};
    ChannelStatus channelStatus{ChannelStatus::ChannelStatusOffline};  // 0: offline, 1: online, 2: auth error
    int dataStatus{0};  // service::camera::AlgDemuxStatus status
    friend void to_json(nlohmann::json& j, const MsgCameraAttr& v);
    friend void from_json(const nlohmann::json& j, MsgCameraAttr& v);
};

struct MsgCameraTask {
    std::string algorithmId;
    std::string algorithmName;
    int enable{0};
    std::string scheduleId;  // Schedule template ID
    std::string scheduleName;
    int status{0};  // CameraTaskStatus 1: running, 0: stopped, -1: paused, 2: error
};

void to_json(nlohmann::json& j, const MsgCameraTask& v);
void from_json(const nlohmann::json& j, MsgCameraTask& v);

enum class MsgCameraType {
    MsgCameraTypeLive = 0  // Live stream
    ,
    MsgCameraTypeVod  // Video on demand
    ,
    MsgCameraTypeOnvif  // Onvif
    ,
    MsgCameraTypeLocalVideo  // Local video file
    ,
    MsgCameraTypeUsb = 6  // USB camera (MJPEG), consistent with old b7a2dcc3
    ,
    MsgCameraTypeMax  //
};

struct MsgCameraInfo : public MsgCameraAttr {
    std::string videoChannelId;
    std::string channelCode;
    std::string channelName;

    std::string channelPic;  // Channel snapshot image
    std::string url;

    MsgCameraType channelType{MsgCameraType::MsgCameraTypeLive};

    std::vector<MsgCameraTask> taskList;
};

void to_json(nlohmann::json& j, const MsgCameraInfo& v);
void from_json(const nlohmann::json& j, MsgCameraInfo& v);

}  // namespace cosmo
