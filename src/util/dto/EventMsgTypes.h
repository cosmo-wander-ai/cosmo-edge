// Event/FaceLib types — MsgConditionEvent, MsgEventUnit, MsgBaseFaceLibInfo, etc.
// Modular DTO header.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct MsgConditionPage {
    int pageNum{1};
    int pageSize{10};
    friend void to_json(nlohmann::json& j, const MsgConditionPage& v);
    friend void from_json(const nlohmann::json& j, MsgConditionPage& v);
};

struct MsgConditionDuration {
    int64_t timeBegin{0};
    int64_t timeEnd{0};
    friend void to_json(nlohmann::json& j, const MsgConditionDuration& v);
    friend void from_json(const nlohmann::json& j, MsgConditionDuration& v);
};

struct MsgConditionEvent : public MsgConditionPage, public MsgConditionDuration {
    std::vector<std::string> algorithmCodes;
    std::vector<std::string> categorys;
    std::string videoChannelName;  // Channel name
    std::string personName;        // Personnel name
    std::string personCode;        // Personnel code
    std::string matchLibName;      // Matched face library

    std::string propColor;         // Target attribute color     used for vehicle body color
    std::string propRelatedColor;  // Subsidiary target attribute color used for license plate color
    std::string propType;          // Target attribute type      used for vehicle type
    std::string propDirection;     // Target attribute direction used for vehicle direction
    int reportStatus{-1};
    std::string language;
};

void to_json(nlohmann::json& j, const MsgConditionEvent& v);
void from_json(const nlohmann::json& j, MsgConditionEvent& v);

struct MsgResultFaceLibInfo {
    std::string failedFaceLibId;
    std::string resMsg;
    uint32_t resCode;
    friend void to_json(nlohmann::json& j, const MsgResultFaceLibInfo& v);
    friend void from_json(const nlohmann::json& j, MsgResultFaceLibInfo& v);
};

// Face library personnel add/edit request
struct MsgConditionLib {
    // default must stay within RangeInt to avoid pre-handler validation failure
    util::RangeInt<1, 2> personOperation{1};
    std::vector<util::String<0, 36>> faceLibId;
    util::String<0, 36> personId;
    util::String<0, 32> personName;
    std::vector<std::string> retainPictureId;
    std::vector<std::string> pictureBase64;
    util::String<0, 50> serialNumber;
    int64_t createTime{0};
    int64_t updateTime{0};
};

void to_json(nlohmann::json& j, const MsgConditionLib& v);
void from_json(const nlohmann::json& j, MsgConditionLib& v);

// Face library basic info
struct MsgBaseFaceLibInfo {
    util::String<0, 36> id;
    util::String<0, 32> name;
    int type{-1};
    util::RangeValue<double> threshold{0.0, 100.0, 82.0};
    util::RangeInt<1, 100000> maxFaceNumber{20000};
    int strangerAlarm{0};
    float strangerThreshold{0.0};
};

void to_json(nlohmann::json& j, const MsgBaseFaceLibInfo& v);
void from_json(const nlohmann::json& j, MsgBaseFaceLibInfo& v);

// Face library detailed info query request
struct MsgQueryFaceLibInfoR {
    util::String<0, 36> cameraId;
    util::RangeInt<1, 207> taskType{2};  // TaskTypeSize
    util::String<0, 32> faceLibName;
    util::String<0, 36> faceLibId;
    int pageNum{1};
    util::RangeInt<-1, 1000> pageSize{10};
    friend void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoR& v);
    friend void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoR& v);
};

// Face library detailed info query response
struct MsgQueryFaceLibInfoS {
    struct FaceLib : public MsgBaseFaceLibInfo {
        FaceLib& operator=(const MsgBaseFaceLibInfo& info) {
            MsgBaseFaceLibInfo::operator=(info);
            return *this;
        }
        int personNumber{0};
        int faceNumber{0};
        int64_t createTimestamp{0};
        int64_t updateTimestamp{0};
    };

    struct ResData {
        int searchAll{0};
        int faceLibCount{0};
        std::vector<FaceLib> faceLibList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoS::FaceLib& v);
void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoS::FaceLib& v);
void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoS& v);
void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoS& v);

// Face library personnel query request
struct MsgQueryFacesR {
    std::vector<std::string> faceLibIdList;
    util::String<0, 36> personId;
    util::String<0, 32> personName;
    util::String<0, 50> serialNumber;
    int pageNum{1};
    util::RangeInt<-1, 10000> pageSize{10};
    std::string queryId;
};

void to_json(nlohmann::json& j, const MsgQueryFacesR& v);
void from_json(const nlohmann::json& j, MsgQueryFacesR& v);

// Face library personnel query response
struct MsgQueryFacesS {
    struct Picture {
        std::string id;
        std::string url;
        friend void to_json(nlohmann::json& j, const Picture& v);
        friend void from_json(const nlohmann::json& j, Picture& v);
    };
    struct FaceLib {
        std::string id;
        std::string name;
        friend void to_json(nlohmann::json& j, const FaceLib& v);
        friend void from_json(const nlohmann::json& j, FaceLib& v);
    };
    struct Person {
        std::string id;
        std::string name;
        int64_t createTimestamp{0};
        int64_t updateTimestamp{0};
        std::vector<FaceLib> faceLibIdList;
        std::vector<Picture> pictureList;
        std::string serialNumber;
        friend void to_json(nlohmann::json& j, const Person& v);
        friend void from_json(const nlohmann::json& j, Person& v);
    };
    struct ResData {
        std::string queryId;
        int totalCount{0};
        std::vector<Person> personList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryFacesS& v);
void from_json(const nlohmann::json& j, MsgQueryFacesS& v);

struct MsgEventUnit {
    std::string id;
    std::string videoChannelId;
    std::string channelCode;
    std::string channelName;
    int64_t timestamp{0};
    std::string category;
    std::string algorithmCode;
    std::string algorithmName;
    std::string areaId;
    std::string areaName;
    std::string fullPicture;
    std::string detectedPicture;
    std::string video;
    std::string videostructured;
    int reportStatus{0};
    std::string property;
    friend void to_json(nlohmann::json& j, const MsgEventUnit& v);
    friend void from_json(const nlohmann::json& j, MsgEventUnit& v);
};

enum class EventRecordFlag { EventRecordVideoFlagNone = 0, EventRecordVideoFlagHave = 1 };

struct AlarmRecordUnit {
    std::string id;
    std::string category;
    //    std::string taskId;                     // Task Id
    std::string videoChannelId;    // Channel
    std::string channelName;       // Channel
    int64_t timestamp{0};          // UTC timestamp ms
    std::string algorithmId;       // Algorithm id
    std::string algorithmName;     // Algorithm name
    std::string areaId;            // Area id
    std::string areaName;          // Area name
    std::string trackId;           // Target trackId
    std::string diskId;            // diskId
    std::string libPersionName;    // Personnel name
    std::string libPersionNumber;  // Personnel number
    std::string libFacesID;        // Belonging face library

    std::string propStr;           // Target attribute string   used for license plate number
    std::string propColor;         // Target attribute color     used for vehicle body color
    std::string propRelatedColor;  // Subsidiary target attribute color used for license plate color
    std::string propType;          // Target attribute type      used for vehicle type
    std::string propDirection;     // Target attribute direction used for vehicle direction
    EventRecordFlag videoFlag{EventRecordFlag::EventRecordVideoFlagNone};
    int64_t eventFrame{-1};
    std::string extraFiles;  // File list   path/id_{name}   This is a json list of {name}
    std::string property;    // property json format
};

struct MsgResultInfo {
    std::string id;
    std::string resMsg;
    uint32_t resCode;
    friend void to_json(nlohmann::json& j, const MsgResultInfo& v);
    friend void from_json(const nlohmann::json& j, MsgResultInfo& v);
};

}  // namespace cosmo
