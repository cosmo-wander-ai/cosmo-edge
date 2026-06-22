#pragma once

#include <system_error>

#include "util/dto/ServerMsgTypes.h"

namespace cosmo::System {

// Picture quality set request
struct MsgSetPictureQualityRecv : public MsgRecvHead {
    util::RangeInt<1, 99> fullPictureQuality{75};
    int32_t areaOverlay{0};
    int32_t targetBoxOverlay{0};
    int32_t targetSizeOverlay{0};
    int32_t alarmNameOverlay{0};
    util::RangeInt<0, 1> gpsInfoOverlay{0};
    util::String<0, 36> overlayText;
};

void to_json(nlohmann::json& j, const MsgSetPictureQualityRecv& v);
void from_json(const nlohmann::json& j, MsgSetPictureQualityRecv& v);

// Picture quality set response
struct MsgSetPictureQualitySend : public MsgSendHead {};

// Picture quality query request
struct MsgQueryPictureQualityRecv : public MsgRecvHead {};

// Picture quality query response
struct MsgQueryPictureQualitySend : public MsgSendHead {
    struct ResData {
        int32_t fullPictureQuality;
        int32_t areaOverlay{0};
        int32_t targetBoxOverlay{0};
        int32_t alarmNameOverlay{0};
        int32_t targetSizeOverlay{0};
        int32_t gpsInfoOverlay{0};
        std::string overlayText;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryPictureQualitySend& v);
void from_json(const nlohmann::json& j, MsgQueryPictureQualitySend& v);

// Picture quality reset request
struct MsgResetPictureQualityRecv : public MsgRecvHead {};

// Picture quality reset response
struct MsgResetPictureQualitySend : public MsgSendHead {};

// Alarm recording time set request
struct MsgSetAlarmVideoDurationRecv : public MsgRecvHead {
    bool enable{true};
    util::RangeInt<1, 9> videoPreTime{5};
    util::RangeInt<1, 9> videoPostTime{5};
};

void to_json(nlohmann::json& j, const MsgSetAlarmVideoDurationRecv& v);
void from_json(const nlohmann::json& j, MsgSetAlarmVideoDurationRecv& v);

// Alarm recording time set response
struct MsgSetAlarmVideoDurationSend : public MsgSendHead {};

// Alarm recording time query request
struct MsgQueryAlarmVideoDurationRecv : public MsgRecvHead {};

// Alarm recording time query response inner data
struct AlarmVideoDurationData {
    bool enable{true};
    int videoPreTime{5};
    int videoPostTime{5};
    friend void to_json(nlohmann::json& j, const AlarmVideoDurationData& v);
    friend void from_json(const nlohmann::json& j, AlarmVideoDurationData& v);
};

// Alarm recording time query response
struct MsgQueryAlarmVideoDurationSend : public MsgSendHead {
    AlarmVideoDurationData resData;
};

void to_json(nlohmann::json& j, const MsgQueryAlarmVideoDurationSend& v);
void from_json(const nlohmann::json& j, MsgQueryAlarmVideoDurationSend& v);

// Alarm recording time reset request
struct MsgResetAlarmVideoDurationRecv : public MsgRecvHead {};

// Alarm recording time reset response
struct MsgResetAlarmVideoDurationSend : public MsgSendHead {};

// Scheduled restart parameters set request
struct MsgModifyDevRestartParamRecv : public MsgRecvHead {
    util::RangeInt<0, 1> isTimingRestart;
    util::RangeInt<0, 7> weekDay{0};
    util::String<0, 6> restartTime;
};

void to_json(nlohmann::json& j, const MsgModifyDevRestartParamRecv& v);
void from_json(const nlohmann::json& j, MsgModifyDevRestartParamRecv& v);

// Scheduled restart parameters set response
struct MsgModifyDevRestartParamSend : public MsgSendHead {};

// Scheduled restart parameters query request
struct MsgQueryDevRestartParamRecv : public MsgRecvHead {};

// Scheduled restart parameters query response
struct MsgQueryDevRestartParamSend : public MsgSendHead {
    struct ResData {
        int isTimingRestart;
        int weekDay{0};
        std::string restartTime;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryDevRestartParamSend& v);
void from_json(const nlohmann::json& j, MsgQueryDevRestartParamSend& v);

// Scheduled restart parameters reset request
struct MsgResetDevRestartParamRecv : public MsgRecvHead {};

// Scheduled restart parameters reset response
struct MsgResetDevRestartParamSend : public MsgSendHead {};

// System Logo query request
struct MsgQuerySystemLogoRecv : public MsgRecvHead {};

// System Logo query response
struct MsgQuerySystemLogoSend : public MsgSendHead {
    struct ResData {
        std::string systemName;
        std::string logoUrl;
        std::string bigScreenName;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQuerySystemLogoSend& v);
void from_json(const nlohmann::json& j, MsgQuerySystemLogoSend& v);

// System Logo set
struct MsgSetSystemLogoRecv : public MsgRecvHead {
    util::String<0, 64> systemName;
    util::String<2, 8> logoFileType;
    std::string logoBase64;
    std::string bigScreenName;
};

void to_json(nlohmann::json& j, const MsgSetSystemLogoRecv& v);
void from_json(const nlohmann::json& j, MsgSetSystemLogoRecv& v);

// System Logo set response
struct MsgSetSystemLogoSend : public MsgSendHead {};

// Popup parameters query
struct MsgQueryPopUpParamRecv : public MsgRecvHead {};

// Popup parameters query response
struct MsgQueryPopUpParamSend : public MsgSendHead {
    struct ResData {
        int popUpSwitch;
        int audioPlay;
        int popUpDuration;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryPopUpParamSend& v);
void from_json(const nlohmann::json& j, MsgQueryPopUpParamSend& v);

// Popup parameters set
struct MsgSetPopUpParamRecv : public MsgRecvHead {
    util::RangeInt<0, 1> popUpSwitch{0};
    util::RangeInt<0, 1> audioPlay{0};
    util::RangeInt<1, 30> popUpDuration{2};
};

void to_json(nlohmann::json& j, const MsgSetPopUpParamRecv& v);
void from_json(const nlohmann::json& j, MsgSetPopUpParamRecv& v);

// Popup parameters set response
struct MsgSetPopUpParamSend : public MsgSendHead {};

// ResourceLimitParam request
struct MsgSetResourceLimitParamRecv : public MsgRecvHead {
    bool enable{true};
};

void to_json(nlohmann::json& j, const MsgSetResourceLimitParamRecv& v);
void from_json(const nlohmann::json& j, MsgSetResourceLimitParamRecv& v);

//
struct MsgSetResourceLimitParamSend : public MsgSendHead {};

//
struct MsgQueryResourceLimitParamRecv : public MsgRecvHead {};

// ResourceLimit query response inner data
struct ResourceLimitParamData {
    bool enable{true};
    friend void to_json(nlohmann::json& j, const ResourceLimitParamData& v);
    friend void from_json(const nlohmann::json& j, ResourceLimitParamData& v);
};

//
struct MsgQueryResourceLimitParamSend : public MsgSendHead {
    ResourceLimitParamData resData;
};

void to_json(nlohmann::json& j, const MsgQueryResourceLimitParamSend& v);
void from_json(const nlohmann::json& j, MsgQueryResourceLimitParamSend& v);

}  // namespace cosmo::System
