// Person lib types — MsgBasePersonLibInfo, MsgQueryPersonInfoRecv, etc.
// Modular DTO header.

#pragma once

#include "util/MsgBaseTypes.h"

namespace cosmo {

//////////////////////////////// Person lib ///////////////////////////////////////////////////////////

struct MsgResultPersonLibInfo {
    std::string failedPersonLibId;
    std::string resMsg;
    uint32_t resCode;
    friend void to_json(nlohmann::json& j, const MsgResultPersonLibInfo& v);
    friend void from_json(const nlohmann::json& j, MsgResultPersonLibInfo& v);
};

// Person lib basic info
struct MsgBasePersonLibInfo {
    util::String<0, 36> id;
    util::String<0, 32> name;
    int type{-1};  // cosmo::db::PersonRecogType::None
    util::RangeValue<double> threshold{0.0, 100.0, 60.0};
    util::RangeInt<1, 1000> maxPersonNumber{1000};
    int strangerAlarm{0};
    float strangerThreshold{0.0};
    friend void to_json(nlohmann::json& j, const MsgBasePersonLibInfo& v);
    friend void from_json(const nlohmann::json& j, MsgBasePersonLibInfo& v);
};

struct MsgQueryPersonInfoRecv {
    util::String<0, 36> cameraId;
    util::RangeInt<1, 207> taskType{58};  // TaskTypeSize
    int personLibType{-1};                // cosmo::db::PersonRecogType::None
    util::String<0, 32> personLibName;
    util::String<0, 36> personLibId;
    int pageNum{1};
    util::RangeInt<-1, 1000> pageSize{10};
    friend void to_json(nlohmann::json& j, const MsgQueryPersonInfoRecv& v);
    friend void from_json(const nlohmann::json& j, MsgQueryPersonInfoRecv& v);
};

struct MsgQueryPersonLibInfoS {
    struct PersonFeatureLib : public MsgBasePersonLibInfo {
        PersonFeatureLib& operator=(const MsgBasePersonLibInfo& info) {
            MsgBasePersonLibInfo::operator=(info);
            return *this;
        }
        int personNumber{0};
        int64_t createTimestamp{0};
        int64_t updateTimestamp{0};
    };

    struct ResData {
        int searchAll{0};
        int personLibCount{0};
        std::vector<PersonFeatureLib> personLibList;
        friend void to_json(nlohmann::json& j, const ResData& v);
        friend void from_json(const nlohmann::json& j, ResData& v);
    } resData;
};

void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoS::PersonFeatureLib& v);
void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoS::PersonFeatureLib& v);
void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoS& v);
void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoS& v);
;

// Person lib member query request
struct MsgQueryPersonPicturesR {
    std::vector<std::string> personLibIdList;
    int personLibType{-1};  // cosmo::db::PersonRecogType::None
    util::String<0, 36> personId;
    util::String<0, 32> pictureName;
    int pageNum{1};
    util::RangeInt<-1, 10000> pageSize{10};
    std::string queryId;
};

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesR& v);
void from_json(const nlohmann::json& j, MsgQueryPersonPicturesR& v);

// Person lib member query response
struct MsgQueryPersonPicturesS {
    struct PersonLib {
        std::string id;
        std::string name;
        friend void to_json(nlohmann::json& j, const PersonLib& v);
        friend void from_json(const nlohmann::json& j, PersonLib& v);
    };
    struct Person {
        std::string id;
        std::string pictureName;
        std::string pictureUrl;
        int64_t createTimestamp{0};
        int64_t updateTimestamp{0};
        std::vector<float> feature;
        PersonLib personLib;
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

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesS& v);
void from_json(const nlohmann::json& j, MsgQueryPersonPicturesS& v);

}  // namespace cosmo
