// ThingsLib and FaceLib types.
// Split from MsgMiscTypes.h for modular compilation.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

struct FaceLibAddInfo {
    std::string name;
    size_t threshold;
    size_t capacity;
    std::string outerId;
    std::string custId;
    friend void to_json(nlohmann::json& j, const FaceLibAddInfo& v);
    friend void from_json(const nlohmann::json& j, FaceLibAddInfo& v);
};

// Things lib basic info
struct MsgBaseThingsLibInfo {
    util::String<0, 36> id;
    util::String<0, 32> name;
    int type{-1};  // enArticlesReidRecogType_None
    util::RangeValue<double> threshold{0.0, 100.0, 60.0};
    util::RangeInt<1, 1000> maxThingsNumber{1000};
    int strangerAlarm{0};
    float strangerThreshold{0.0};
    friend void to_json(nlohmann::json& j, const MsgBaseThingsLibInfo& v);
    friend void from_json(const nlohmann::json& j, MsgBaseThingsLibInfo& v);
};

struct MsgResultThingsLibInfo {
    std::string failedThingsLibId;
    std::string resMsg;
    uint32_t resCode;
    friend void to_json(nlohmann::json& j, const MsgResultThingsLibInfo& v);
    friend void from_json(const nlohmann::json& j, MsgResultThingsLibInfo& v);
};

// Things lib item query request
struct MsgQueryThingsPicturesR {
    std::vector<std::string> thingsLibIdList;  // Things ReID lib IDs
    int thingsLibType{-1};                     // enArtiClesReidType_None
    util::String<0, 36> thingsId;
    util::String<0, 32> pictureName;
    int pageNum{1};
    util::RangeInt<-1, 10000> pageSize{10};
    std::string queryId;
};

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesR& v);
void from_json(const nlohmann::json& j, MsgQueryThingsPicturesR& v);

// Things lib item query response
struct MsgQueryThingsPicturesS {
    struct ArticlesReidLib {
        std::string id;
        std::string name;
        friend void to_json(nlohmann::json& j, const ArticlesReidLib& v);
        friend void from_json(const nlohmann::json& j, ArticlesReidLib& v);
    };
    struct ArticlesReid {
        std::string id;
        std::string pictureName;
        std::string pictureUrl;
        int64_t createTimestamp{0};
        int64_t updateTimestamp{0};
        std::vector<float> feature;
        ArticlesReidLib thingsLib;
        friend void to_json(nlohmann::json& j, const ArticlesReid& v);
        friend void from_json(const nlohmann::json& j, ArticlesReid& v);
    };
    struct ResData {
        std::string queryId;
        int totalCount{0};
        std::vector<ArticlesReid> thingsList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesS& v);
void from_json(const nlohmann::json& j, MsgQueryThingsPicturesS& v);

// Things lib detail query request
struct MsgQueryThingsLibInfoR {
    util::String<0, 36> cameraId;
    util::RangeInt<1, 207> taskType{148};  // TaskTypeSize
    int thingsLibType{-1};                 // cosmo::db::ArticlesReidType::None
    util::String<0, 32> thingsLibName;
    util::String<0, 36> thingsLibId;
    int pageNum{1};
    util::RangeInt<-1, 1000> pageSize{10};
    friend void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoR& v);
    friend void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoR& v);
};

// Things lib detail query response
struct MsgQueryThingsLibInfoS {
    struct ThingsFeatureLib : public MsgBaseThingsLibInfo {
        ThingsFeatureLib& operator=(const MsgBaseThingsLibInfo& info) {
            MsgBaseThingsLibInfo::operator=(info);
            return *this;
        }
        int thingsNumber{0};
        int64_t createTimestamp{0};
        int64_t updateTimestamp{0};
    };

    struct ResData {
        int searchAll{0};
        int thingsLibCount{0};
        std::vector<ThingsFeatureLib> thingsLibList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoS::ThingsFeatureLib& v);
void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoS::ThingsFeatureLib& v);
void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoS& v);
void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoS& v);

}  // namespace cosmo
