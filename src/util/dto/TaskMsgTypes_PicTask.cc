// TaskMsgTypes_PicTask — Task Msg Types_ Pic Task implementation.

#include <nlohmann/json.hpp>

#include "TaskCreateTypes.h"
#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Picture task and alarm video serialization (split from TaskMsgTypes.cc)
namespace cosmo {

void to_json(nlohmann::json& j, const MsgPTaskCreateRecv& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["algorithmCode"]       = r.algorithmCode;
    j["algorithmUpdateTime"] = r.algorithmUpdateTime;
    j["taskId"]              = r.taskId;
    j["mvDebug"]             = r.mvDebug;
    j["taskDesc"]            = r.taskDesc;
    j["algorithmId"]         = r.algorithmId;
    j["algorithmCategory"]   = r.algorithmCategory;
    j["algorithmVersion"]    = r.algorithmVersion;
    j["algorithmName"]       = r.algorithmName;
    j["algorithmCheckSum"]   = r.algorithmCheckSum;
    j["taskConfig"]          = r.taskConfig;
}

void from_json(const nlohmann::json& j, MsgPTaskCreateRecv& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    j.at("algorithmCode").get_to(r.algorithmCode);              // mandatory
    j.at("algorithmUpdateTime").get_to(r.algorithmUpdateTime);  // mandatory
    JSON_OPT(j, r, taskId);
    JSON_OPT(j, r, mvDebug);
    JSON_OPT(j, r, taskDesc);
    JSON_OPT(j, r, algorithmId);
    JSON_OPT(j, r, algorithmCategory);
    JSON_OPT(j, r, algorithmVersion);
    JSON_OPT(j, r, algorithmName);
    JSON_OPT(j, r, algorithmCheckSum);
    JSON_OPT(j, r, taskConfig);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicRecv& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["algorithmCode"] = r.algorithmCode;
    j["taskId"]        = r.taskId;
    j["mvDebug"]       = r.mvDebug;
    j["imageBase64"]   = r.imageBase64;
    j["imageUrl"]      = r.imageUrl;
    j["taskConfig"]    = r.taskConfig;
}

void from_json(const nlohmann::json& j, MsgPTaskDetectPicRecv& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    j.at("algorithmCode").get_to(r.algorithmCode);  // mandatory
    JSON_OPT(j, r, taskId);
    JSON_OPT(j, r, mvDebug);
    JSON_OPT(j, r, imageBase64);
    JSON_OPT(j, r, imageUrl);
    JSON_OPT(j, r, taskConfig);
}

void to_json(nlohmann::json& j, const MsgPTaskTarget& t) {
    j["box"] = t.box;
    if (t.bHaveLogicResult)
        j["bLogicResult"] = t.bLogicResult;
    if (!t.confidence.empty())
        j["confidence"] = t.confidence;
    if (!t.groupEls.empty())
        j["groupEls"] = t.groupEls;
    if (t.bHaveMatchInfo)
        j["matchInfo"] = t.matchInfo;
    if (!t.maskPolygon.empty())
        j["maskPolygon"] = t.maskPolygon;
    if (!t.landmark.empty())
        j["landmark"] = t.landmark;
    if (!t.featurePreview.empty())
        j["featurePreview"] = t.featurePreview;
}

void from_json(const nlohmann::json& j, MsgPTaskTarget& t) {
    JSON_OPT(j, t, box);
    if (auto it = j.find("bLogicResult"); it != j.end() && !it->is_null()) {
        t.bHaveLogicResult = true;
        it->get_to(t.bLogicResult);
    }
    JSON_OPT(j, t, confidence);
    JSON_OPT(j, t, groupEls);
    if (auto it = j.find("matchInfo"); it != j.end() && !it->is_null()) {
        t.bHaveMatchInfo = true;
        it->get_to(t.matchInfo);
    }
    JSON_OPT(j, t, maskPolygon);
    JSON_OPT(j, t, landmark);
    JSON_OPT(j, t, featurePreview);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicSend& s) {
    to_json(j, static_cast<const MsgSendHead&>(s));
    j["resData"] = s.resData;
}

void from_json(const nlohmann::json& j, MsgPTaskDetectPicSend& s) {
    from_json(j, static_cast<MsgSendHead&>(s));
    JSON_OPT(j, s, resData);
}

void from_json(const nlohmann::json& j, MsgAlarmVideoOverviewFrame& v) {
    JSON_OPT(j, v, index);
    JSON_OPT(j, v, color);
    JSON_OPT(j, v, rects);
}

void to_json(nlohmann::json& j, const MsgAlarmVideoOverviewFrame& v) {
    j["index"] = v.index;
    j["color"] = v.color;
    j["rects"] = v.rects;
}

void from_json(const nlohmann::json& j, MsgAlarmVideoOverviewInfo& v) {
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, area);
    JSON_OPT(j, v, targets);
}

void to_json(nlohmann::json& j, const MsgAlarmVideoOverviewInfo& v) {
    j["algorithmCode"] = v.algorithmCode;
    j["area"]          = v.area;
    j["targets"]       = v.targets;
}

void from_json(const nlohmann::json& j, MsgPTaskArea& v) {
    JSON_OPT(j, v, areaId);
    JSON_OPT(j, v, areaName);
    JSON_OPT(j, v, bDetected);
    JSON_OPT(j, v, targetList);
}

void to_json(nlohmann::json& j, const MsgPTaskArea& v) {
    j["areaId"]     = v.areaId;
    j["areaName"]   = v.areaName;
    j["bDetected"]  = v.bDetected;
    j["targetList"] = v.targetList;
}

void from_json(const nlohmann::json& j, MsgPTaskDetectPicSend::ResData& v) {
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, fullPicture);
    JSON_OPT(j, v, areaList);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicSend::ResData& v) {
    j["algorithmCode"] = v.algorithmCode;
    j["timestamp"]     = v.timestamp;
    j["fullPicture"]   = v.fullPicture;
    j["areaList"]      = v.areaList;
}

}  // namespace cosmo
