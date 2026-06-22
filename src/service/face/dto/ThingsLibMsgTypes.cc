// ThingsLibMsgTypes — ThingsLib and FaceLib types.

#include "ThingsLibMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgQueryThingsPicturesR& v) {
    j["thingsLibIdList"] = v.thingsLibIdList;
    j["thingsLibType"]   = v.thingsLibType;
    j["thingsId"]        = v.thingsId;
    j["pictureName"]     = v.pictureName;
    j["pageNum"]         = v.pageNum;
    j["pageSize"]        = v.pageSize;
    j["queryId"]         = v.queryId;
}

void from_json(const nlohmann::json& j, MsgQueryThingsPicturesR& v) {
    j.at("thingsLibIdList").get_to(v.thingsLibIdList);
    JSON_OPT(j, v, thingsLibType);
    JSON_OPT(j, v, thingsId);
    JSON_OPT(j, v, pictureName);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
    JSON_OPT(j, v, queryId);
}

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesS& v) {
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryThingsPicturesS& v) {
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoS::ThingsFeatureLib& v) {
    to_json(j, static_cast<const MsgBaseThingsLibInfo&>(v));
    j["thingsNumber"]    = v.thingsNumber;
    j["createTimestamp"] = v.createTimestamp;
    j["updateTimestamp"] = v.updateTimestamp;
}

void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoS::ThingsFeatureLib& v) {
    from_json(j, static_cast<MsgBaseThingsLibInfo&>(v));
    JSON_OPT(j, v, thingsNumber);
    JSON_OPT(j, v, createTimestamp);
    JSON_OPT(j, v, updateTimestamp);
}

void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoS& v) {
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoS& v) {
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, FaceLibAddInfo& v) {
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, threshold);
    JSON_OPT(j, v, capacity);
    JSON_OPT(j, v, outerId);
    JSON_OPT(j, v, custId);
}

void to_json(nlohmann::json& j, const FaceLibAddInfo& v) {
    j["name"]      = v.name;
    j["threshold"] = v.threshold;
    j["capacity"]  = v.capacity;
    j["outerId"]   = v.outerId;
    j["custId"]    = v.custId;
}

void from_json(const nlohmann::json& j, MsgResultThingsLibInfo& v) {
    JSON_OPT(j, v, failedThingsLibId);
    JSON_OPT(j, v, resCode);
    JSON_OPT(j, v, resMsg);
}

void to_json(nlohmann::json& j, const MsgResultThingsLibInfo& v) {
    j["failedThingsLibId"] = v.failedThingsLibId;
    j["resCode"]           = v.resCode;
    j["resMsg"]            = v.resMsg;
}

void from_json(const nlohmann::json& j, MsgQueryThingsPicturesS::ArticlesReidLib& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
}

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesS::ArticlesReidLib& v) {
    j["id"]   = v.id;
    j["name"] = v.name;
}

void from_json(const nlohmann::json& j, MsgQueryThingsPicturesS::ArticlesReid& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, pictureName);
    JSON_OPT(j, v, pictureUrl);
    JSON_OPT(j, v, createTimestamp);
    JSON_OPT(j, v, updateTimestamp);
    JSON_OPT(j, v, thingsLib);
}

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesS::ArticlesReid& v) {
    j["id"]              = v.id;
    j["pictureName"]     = v.pictureName;
    j["pictureUrl"]      = v.pictureUrl;
    j["createTimestamp"] = v.createTimestamp;
    j["updateTimestamp"] = v.updateTimestamp;
    j["thingsLib"]       = v.thingsLib;
}

void from_json(const nlohmann::json& j, MsgQueryThingsPicturesS::ResData& v) {
    JSON_OPT(j, v, queryId);
    JSON_OPT(j, v, totalCount);
    JSON_OPT(j, v, thingsList);
}

void to_json(nlohmann::json& j, const MsgQueryThingsPicturesS::ResData& v) {
    j["queryId"]    = v.queryId;
    j["totalCount"] = v.totalCount;
    j["thingsList"] = v.thingsList;
}

void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoS::ResData& v) {
    JSON_OPT(j, v, searchAll);
    JSON_OPT(j, v, thingsLibCount);
    JSON_OPT(j, v, thingsLibList);
}

void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoS::ResData& v) {
    j["searchAll"]      = v.searchAll;
    j["thingsLibCount"] = v.thingsLibCount;
    j["thingsLibList"]  = v.thingsLibList;
}

void to_json(nlohmann::json& j, const MsgBaseThingsLibInfo& v) {
    j["id"]                = v.id;
    j["name"]              = v.name;
    j["type"]              = v.type;
    j["threshold"]         = v.threshold;
    j["maxThingsNumber"]   = v.maxThingsNumber;
    j["strangerAlarm"]     = v.strangerAlarm;
    j["strangerThreshold"] = v.strangerThreshold;
}

void from_json(const nlohmann::json& j, MsgBaseThingsLibInfo& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, type);
    JSON_OPT(j, v, threshold);
    JSON_OPT(j, v, maxThingsNumber);
    JSON_OPT(j, v, strangerAlarm);
    JSON_OPT(j, v, strangerThreshold);
}

void to_json(nlohmann::json& j, const MsgQueryThingsLibInfoR& v) {
    j["cameraId"]      = v.cameraId;
    j["taskType"]      = v.taskType;
    j["thingsLibType"] = v.thingsLibType;
    j["thingsLibName"] = v.thingsLibName;
    j["pageNum"]       = v.pageNum;
    j["pageSize"]      = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgQueryThingsLibInfoR& v) {
    JSON_OPT(j, v, cameraId);
    JSON_OPT(j, v, taskType);
    JSON_OPT(j, v, thingsLibType);
    JSON_OPT(j, v, thingsLibName);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

}  // namespace cosmo
