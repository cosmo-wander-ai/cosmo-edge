// PersonMsgTypes — Person lib types — MsgBasePersonLibInfo, MsgQueryPersonInfoRecv, etc.

#include "PersonMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoS::PersonFeatureLib& v) {
    to_json(j, static_cast<const MsgBasePersonLibInfo&>(v));
    j["personNumber"]    = v.personNumber;
    j["createTimestamp"] = v.createTimestamp;
    j["updateTimestamp"] = v.updateTimestamp;
}

void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoS::PersonFeatureLib& v) {
    from_json(j, static_cast<MsgBasePersonLibInfo&>(v));
    JSON_OPT(j, v, personNumber);
    JSON_OPT(j, v, createTimestamp);
    JSON_OPT(j, v, updateTimestamp);
}

void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoS& v) {
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoS& v) {
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesR& v) {
    j["personLibIdList"] = v.personLibIdList;
    j["personLibType"]   = v.personLibType;
    j["personId"]        = v.personId;
    j["pictureName"]     = v.pictureName;
    j["pageNum"]         = v.pageNum;
    j["pageSize"]        = v.pageSize;
    j["queryId"]         = v.queryId;
}

void from_json(const nlohmann::json& j, MsgQueryPersonPicturesR& v) {
    j.at("personLibIdList").get_to(v.personLibIdList);
    JSON_OPT(j, v, personLibType);
    JSON_OPT(j, v, personId);
    JSON_OPT(j, v, pictureName);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
    JSON_OPT(j, v, queryId);
}

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesS& v) {
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryPersonPicturesS& v) {
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgResultPersonLibInfo& v) {
    JSON_OPT(j, v, failedPersonLibId);
    JSON_OPT(j, v, resCode);
    JSON_OPT(j, v, resMsg);
}

void to_json(nlohmann::json& j, const MsgResultPersonLibInfo& v) {
    j["failedPersonLibId"] = v.failedPersonLibId;
    j["resCode"]           = v.resCode;
    j["resMsg"]            = v.resMsg;
}

void from_json(const nlohmann::json& j, MsgQueryPersonLibInfoS::ResData& v) {
    JSON_OPT(j, v, searchAll);
    JSON_OPT(j, v, personLibCount);
    JSON_OPT(j, v, personLibList);
}

void to_json(nlohmann::json& j, const MsgQueryPersonLibInfoS::ResData& v) {
    j["searchAll"]      = v.searchAll;
    j["personLibCount"] = v.personLibCount;
    j["personLibList"]  = v.personLibList;
}

void from_json(const nlohmann::json& j, MsgQueryPersonPicturesS::PersonLib& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
}

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesS::PersonLib& v) {
    j["id"]   = v.id;
    j["name"] = v.name;
}

void from_json(const nlohmann::json& j, MsgQueryPersonPicturesS::Person& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, pictureName);
    JSON_OPT(j, v, pictureUrl);
    JSON_OPT(j, v, createTimestamp);
    JSON_OPT(j, v, updateTimestamp);
    JSON_OPT(j, v, personLib);
}

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesS::Person& v) {
    j["id"]              = v.id;
    j["pictureName"]     = v.pictureName;
    j["pictureUrl"]      = v.pictureUrl;
    j["createTimestamp"] = v.createTimestamp;
    j["updateTimestamp"] = v.updateTimestamp;
    j["personLib"]       = v.personLib;
}

void from_json(const nlohmann::json& j, MsgQueryPersonPicturesS::ResData& v) {
    JSON_OPT(j, v, queryId);
    JSON_OPT(j, v, totalCount);
    JSON_OPT(j, v, personList);
}

void to_json(nlohmann::json& j, const MsgQueryPersonPicturesS::ResData& v) {
    j["queryId"]    = v.queryId;
    j["totalCount"] = v.totalCount;
    j["personList"] = v.personList;
}

void to_json(nlohmann::json& j, const MsgBasePersonLibInfo& v) {
    j["id"]                = v.id;
    j["name"]              = v.name;
    j["type"]              = v.type;
    j["threshold"]         = v.threshold;
    j["maxPersonNumber"]   = v.maxPersonNumber;
    j["strangerAlarm"]     = v.strangerAlarm;
    j["strangerThreshold"] = v.strangerThreshold;
}

void from_json(const nlohmann::json& j, MsgBasePersonLibInfo& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, type);
    JSON_OPT(j, v, threshold);
    JSON_OPT(j, v, maxPersonNumber);
    JSON_OPT(j, v, strangerAlarm);
    JSON_OPT(j, v, strangerThreshold);
}

void to_json(nlohmann::json& j, const MsgQueryPersonInfoRecv& v) {
    j["cameraId"]      = v.cameraId;
    j["taskType"]      = v.taskType;
    j["personLibType"] = v.personLibType;
    j["personLibName"] = v.personLibName;
    j["pageNum"]       = v.pageNum;
    j["pageSize"]      = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgQueryPersonInfoRecv& v) {
    JSON_OPT(j, v, cameraId);
    JSON_OPT(j, v, taskType);
    JSON_OPT(j, v, personLibType);
    JSON_OPT(j, v, personLibName);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

}  // namespace cosmo
