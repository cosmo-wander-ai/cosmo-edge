// EventMsgTypes — Event/FaceLib types — MsgConditionEvent, MsgEventUnit, MsgBaseFaceLibInfo, etc.

#include "EventMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgConditionEvent& v) {
    to_json(j, static_cast<const MsgConditionPage&>(v));
    to_json(j, static_cast<const MsgConditionDuration&>(v));
    j["algorithmCodes"]   = v.algorithmCodes;
    j["categorys"]        = v.categorys;
    j["videoChannelName"] = v.videoChannelName;
    j["personName"]       = v.personName;
    j["personCode"]       = v.personCode;
    j["matchLibName"]     = v.matchLibName;
    j["reportStatus"]     = v.reportStatus;
    j["propColor"]        = v.propColor;
    j["propRelatedColor"] = v.propRelatedColor;
    j["propType"]         = v.propType;
    j["propDirection"]    = v.propDirection;
    j["language"]         = v.language;
}

void from_json(const nlohmann::json& j, MsgConditionEvent& v) {
    from_json(j, static_cast<MsgConditionPage&>(v));
    from_json(j, static_cast<MsgConditionDuration&>(v));
    JSON_OPT(j, v, algorithmCodes);
    JSON_OPT(j, v, categorys);
    JSON_OPT(j, v, videoChannelName);
    JSON_OPT(j, v, personName);
    JSON_OPT(j, v, personCode);
    JSON_OPT(j, v, matchLibName);
    JSON_OPT(j, v, reportStatus);
    JSON_OPT(j, v, propColor);
    JSON_OPT(j, v, propRelatedColor);
    JSON_OPT(j, v, propType);
    JSON_OPT(j, v, propDirection);
    JSON_OPT(j, v, language);
}

void to_json(nlohmann::json& j, const MsgConditionLib& v) {
    j["personOperation"] = v.personOperation;
    j["faceLibId"]       = v.faceLibId;
    j["personId"]        = v.personId;
    j["personName"]      = v.personName;
    j["pictureBase64"]   = v.pictureBase64;
    j["retainPictureId"] = v.retainPictureId;
    j["serialNumber"]    = v.serialNumber;
}

void from_json(const nlohmann::json& j, MsgConditionLib& v) {
    j.at("personOperation").get_to(v.personOperation);  // mandatory
    j.at("faceLibId").get_to(v.faceLibId);              // mandatory
    JSON_OPT(j, v, personId);
    JSON_OPT(j, v, personName);
    JSON_OPT(j, v, pictureBase64);
    JSON_OPT(j, v, retainPictureId);
    JSON_OPT(j, v, serialNumber);
}

void to_json(nlohmann::json& j, const MsgBaseFaceLibInfo& v) {
    j["id"]                = v.id;
    j["name"]              = v.name;
    j["type"]              = v.type;
    j["threshold"]         = v.threshold;
    j["maxFaceNumber"]     = v.maxFaceNumber;
    j["strangerAlarm"]     = v.strangerAlarm;
    j["strangerThreshold"] = v.strangerThreshold;
}

void from_json(const nlohmann::json& j, MsgBaseFaceLibInfo& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, type);
    JSON_OPT(j, v, threshold);
    JSON_OPT(j, v, maxFaceNumber);
    JSON_OPT(j, v, strangerAlarm);
    JSON_OPT(j, v, strangerThreshold);
}

void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoS::FaceLib& v) {
    to_json(j, static_cast<const MsgBaseFaceLibInfo&>(v));
    j["personNumber"]    = v.personNumber;
    j["faceNumber"]      = v.faceNumber;
    j["createTimestamp"] = v.createTimestamp;
    j["updateTimestamp"] = v.updateTimestamp;
}

void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoS::FaceLib& v) {
    from_json(j, static_cast<MsgBaseFaceLibInfo&>(v));
    JSON_OPT(j, v, personNumber);
    JSON_OPT(j, v, faceNumber);
    JSON_OPT(j, v, createTimestamp);
    JSON_OPT(j, v, updateTimestamp);
}

void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoS& v) {
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoS& v) {
    JSON_OPT(j, v, resData);
}

void to_json(nlohmann::json& j, const MsgQueryFacesR& v) {
    j["faceLibIdList"] = v.faceLibIdList;
    j["personId"]      = v.personId;
    j["personName"]    = v.personName;
    j["serialNumber"]  = v.serialNumber;
    j["pageNum"]       = v.pageNum;
    j["pageSize"]      = v.pageSize;
    j["queryId"]       = v.queryId;
}

void from_json(const nlohmann::json& j, MsgQueryFacesR& v) {
    j.at("faceLibIdList").get_to(v.faceLibIdList);  // mandatory
    JSON_OPT(j, v, personId);
    JSON_OPT(j, v, personName);
    JSON_OPT(j, v, serialNumber);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
    JSON_OPT(j, v, queryId);
}

void to_json(nlohmann::json& j, const MsgQueryFacesS& v) {
    j["resData"] = v.resData;
}

void from_json(const nlohmann::json& j, MsgQueryFacesS& v) {
    JSON_OPT(j, v, resData);
}

void from_json(const nlohmann::json& j, MsgConditionPage& v) {
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

void to_json(nlohmann::json& j, const MsgConditionPage& v) {
    j["pageNum"]  = v.pageNum;
    j["pageSize"] = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgConditionDuration& v) {
    JSON_OPT(j, v, timeBegin);
    JSON_OPT(j, v, timeEnd);
}

void to_json(nlohmann::json& j, const MsgConditionDuration& v) {
    j["timeBegin"] = v.timeBegin;
    j["timeEnd"]   = v.timeEnd;
}

void from_json(const nlohmann::json& j, MsgResultFaceLibInfo& v) {
    JSON_OPT(j, v, failedFaceLibId);
    JSON_OPT(j, v, resCode);
    JSON_OPT(j, v, resMsg);
}

void to_json(nlohmann::json& j, const MsgResultFaceLibInfo& v) {
    j["failedFaceLibId"] = v.failedFaceLibId;
    j["resCode"]         = v.resCode;
    j["resMsg"]          = v.resMsg;
}

void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoS::ResData& v) {
    JSON_OPT(j, v, searchAll);
    JSON_OPT(j, v, faceLibCount);
    JSON_OPT(j, v, faceLibList);
}

void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoS::ResData& v) {
    j["searchAll"]    = v.searchAll;
    j["faceLibCount"] = v.faceLibCount;
    j["faceLibList"]  = v.faceLibList;
}

void from_json(const nlohmann::json& j, MsgQueryFacesS::Picture& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, url);
}

void to_json(nlohmann::json& j, const MsgQueryFacesS::Picture& v) {
    j["id"]  = v.id;
    j["url"] = v.url;
}

void from_json(const nlohmann::json& j, MsgQueryFacesS::FaceLib& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
}

void to_json(nlohmann::json& j, const MsgQueryFacesS::FaceLib& v) {
    j["id"]   = v.id;
    j["name"] = v.name;
}

void from_json(const nlohmann::json& j, MsgQueryFacesS::Person& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, name);
    JSON_OPT(j, v, createTimestamp);
    JSON_OPT(j, v, updateTimestamp);
    JSON_OPT(j, v, faceLibIdList);
    JSON_OPT(j, v, pictureList);
    JSON_OPT(j, v, serialNumber);
}

void to_json(nlohmann::json& j, const MsgQueryFacesS::Person& v) {
    j["id"]              = v.id;
    j["name"]            = v.name;
    j["createTimestamp"] = v.createTimestamp;
    j["updateTimestamp"] = v.updateTimestamp;
    j["faceLibIdList"]   = v.faceLibIdList;
    j["pictureList"]     = v.pictureList;
    j["serialNumber"]    = v.serialNumber;
}

void from_json(const nlohmann::json& j, MsgQueryFacesS::ResData& v) {
    JSON_OPT(j, v, queryId);
    JSON_OPT(j, v, totalCount);
    JSON_OPT(j, v, personList);
}

void to_json(nlohmann::json& j, const MsgQueryFacesS::ResData& v) {
    j["queryId"]    = v.queryId;
    j["totalCount"] = v.totalCount;
    j["personList"] = v.personList;
}

void from_json(const nlohmann::json& j, MsgEventUnit& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, videoChannelId);
    JSON_OPT(j, v, channelCode);
    JSON_OPT(j, v, channelName);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, category);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, algorithmName);
    JSON_OPT(j, v, areaId);
    JSON_OPT(j, v, areaName);
    JSON_OPT(j, v, fullPicture);
    JSON_OPT(j, v, detectedPicture);
    JSON_OPT(j, v, video);
    JSON_OPT(j, v, videostructured);
    JSON_OPT(j, v, reportStatus);
    JSON_OPT(j, v, property);
}

void to_json(nlohmann::json& j, const MsgEventUnit& v) {
    j["id"]              = v.id;
    j["videoChannelId"]  = v.videoChannelId;
    j["channelCode"]     = v.channelCode;
    j["channelName"]     = v.channelName;
    j["timestamp"]       = v.timestamp;
    j["category"]        = v.category;
    j["algorithmCode"]   = v.algorithmCode;
    j["algorithmName"]   = v.algorithmName;
    j["areaId"]          = v.areaId;
    j["areaName"]        = v.areaName;
    j["fullPicture"]     = v.fullPicture;
    j["detectedPicture"] = v.detectedPicture;
    j["video"]           = v.video;
    j["videostructured"] = v.videostructured;
    j["reportStatus"]    = v.reportStatus;
    j["property"]        = v.property;
}

void from_json(const nlohmann::json& j, MsgResultInfo& v) {
    JSON_OPT(j, v, id);
    JSON_OPT(j, v, resCode);
    JSON_OPT(j, v, resMsg);
}

void to_json(nlohmann::json& j, const MsgResultInfo& v) {
    j["id"]      = v.id;
    j["resCode"] = v.resCode;
    j["resMsg"]  = v.resMsg;
}

void to_json(nlohmann::json& j, const MsgQueryFaceLibInfoR& v) {
    j["cameraId"]    = v.cameraId;
    j["taskType"]    = v.taskType;
    j["faceLibName"] = v.faceLibName;
    j["faceLibId"]   = v.faceLibId;
    j["pageNum"]     = v.pageNum;
    j["pageSize"]    = v.pageSize;
}

void from_json(const nlohmann::json& j, MsgQueryFaceLibInfoR& v) {
    JSON_OPT(j, v, cameraId);
    JSON_OPT(j, v, taskType);
    JSON_OPT(j, v, faceLibName);
    JSON_OPT(j, v, faceLibId);
    JSON_OPT(j, v, pageNum);
    JSON_OPT(j, v, pageSize);
}

}  // namespace cosmo
