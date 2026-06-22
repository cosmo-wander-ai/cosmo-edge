// TaskMsgTypes_TaskCreate — Task Msg Types_ Task Create implementation.

#include <nlohmann/json.hpp>

#include "TaskCreateTypes.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Task creation and config serialization (split from TaskMsgTypes.cc)
namespace cosmo {

void to_json(nlohmann::json& j, const MsgTaskCreateRecv& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["taskId"]              = r.taskId;
    j["videoChannelId"]      = r.videoChannelId;
    j["algorithmCode"]       = r.algorithmCode;
    j["algorithmUpdateTime"] = r.algorithmUpdateTime;
    j["mvDebug"]             = r.mvDebug;
    j["taskDesc"]            = r.taskDesc;
    j["videoChannelName"]    = r.videoChannelName;
    j["streamUrl"]           = r.streamUrl;
    j["algorithmId"]         = r.algorithmId;
    j["algorithmCategory"]   = r.algorithmCategory;
    j["algorithmVersion"]    = r.algorithmVersion;
    j["algorithmName"]       = r.algorithmName;
    j["algorithmCheckSum"]   = r.algorithmCheckSum;
    j["taskConfig"]          = r.taskConfig;
}

void from_json(const nlohmann::json& j, MsgTaskCreateRecv& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    j.at("taskId").get_to(r.taskId);                            // mandatory
    j.at("videoChannelId").get_to(r.videoChannelId);            // mandatory
    j.at("algorithmCode").get_to(r.algorithmCode);              // mandatory
    j.at("algorithmUpdateTime").get_to(r.algorithmUpdateTime);  // mandatory
    JSON_OPT(j, r, mvDebug);
    JSON_OPT(j, r, taskDesc);
    JSON_OPT(j, r, videoChannelName);
    JSON_OPT(j, r, streamUrl);
    JSON_OPT(j, r, algorithmId);
    JSON_OPT(j, r, algorithmCategory);
    JSON_OPT(j, r, algorithmVersion);
    JSON_OPT(j, r, algorithmName);
    JSON_OPT(j, r, algorithmCheckSum);
    JSON_OPT(j, r, taskConfig);
}

void from_json(const nlohmann::json& j, MsgTaskFaceSetConfig& v) {
    JSON_OPT(j, v, faceSetToken);
    JSON_OPT(j, v, faceSetThreshold);
    JSON_OPT(j, v, nodeN);
    JSON_OPT(j, v, faceSetVersion);
}

void to_json(nlohmann::json& j, const MsgTaskFaceSetConfig& v) {
    j["faceSetToken"]     = v.faceSetToken;
    j["faceSetThreshold"] = v.faceSetThreshold;
    j["nodeN"]            = v.nodeN;
    j["faceSetVersion"]   = v.faceSetVersion;
}

void from_json(const nlohmann::json& j, MsgAiConfidence& v) {
    JSON_OPT(j, v, label);
    JSON_OPT(j, v, confidence);
}

void to_json(nlohmann::json& j, const MsgAiConfidence& v) {
    j["label"]      = v.label;
    j["confidence"] = v.confidence;
}

void from_json(const nlohmann::json& j, MsgAiAttribute& v) {
    JSON_OPT(j, v, category);
    JSON_OPT(j, v, label);
}

void to_json(nlohmann::json& j, const MsgAiAttribute& v) {
    j["category"] = v.category;
    j["label"]    = v.label;
}

void from_json(const nlohmann::json& j, MsgMatchInfo& v) {
    JSON_OPT(j, v, setPicCount);
    JSON_OPT(j, v, matchId);
    JSON_OPT(j, v, matchDegree);
    JSON_OPT(j, v, groupId);
    JSON_OPT(j, v, groupName);
    JSON_OPT(j, v, matched);
}

void to_json(nlohmann::json& j, const MsgMatchInfo& v) {
    j["setPicCount"] = v.setPicCount;
    j["matchId"]     = v.matchId;
    j["matchDegree"] = v.matchDegree;
    j["groupId"]     = v.groupId;
    j["groupName"]   = v.groupName;
    j["matched"]     = v.matched;
}

void from_json(const nlohmann::json& j, MsgTaskConfig& v) {
    JSON_OPT(j, v, facesetConfig);
    JSON_OPT(j, v, areas);
    JSON_OPT(j, v, shieldedAreas);
    JSON_OPT(j, v, params);
}

void to_json(nlohmann::json& j, const MsgTaskConfig& v) {
    j["facesetConfig"] = v.facesetConfig;
    j["areas"]         = v.areas;
    j["shieldedAreas"] = v.shieldedAreas;
    j["params"]        = v.params;
}

}  // namespace cosmo
