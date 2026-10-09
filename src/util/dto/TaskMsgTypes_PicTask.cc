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

void to_json(nlohmann::json& j, const MsgPictureInput& v) {
    j = {{"imageBase64", v.imageBase64}, {"imageUrl", v.imageUrl}, {"uploadId", v.uploadId}};
}
void from_json(const nlohmann::json& j, MsgPictureInput& v) {
    JSON_OPT(j, v, imageBase64);
    JSON_OPT(j, v, imageUrl);
    JSON_OPT(j, v, uploadId);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicRecv& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["algorithmCode"] = r.algorithmCode;
    j["taskId"]        = r.taskId;
    j["mvDebug"]       = r.mvDebug;
    j["imageBase64"]   = r.imageBase64;
    if (r.referenceImage.HasInput())
        j["referenceImage"] = r.referenceImage;
    j["imageUrl"]   = r.imageUrl;
    j["uploadId"]   = r.uploadId;
    j["taskConfig"] = r.taskConfig;
    j["needRetImg"] = r.needRetImg;
    j["resultMode"] = r.resultMode;
    j["requestId"]  = r.requestId;
}

void from_json(const nlohmann::json& j, MsgPTaskDetectPicRecv& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    j.at("algorithmCode").get_to(r.algorithmCode);  // mandatory
    JSON_OPT(j, r, taskId);
    JSON_OPT(j, r, mvDebug);
    JSON_OPT(j, r, imageBase64);
    JSON_OPT(j, r, referenceImage);
    JSON_OPT(j, r, imageUrl);
    JSON_OPT(j, r, uploadId);
    JSON_OPT(j, r, taskConfig);
    JSON_OPT(j, r, needRetImg);
    JSON_OPT(j, r, resultMode);
    JSON_OPT(j, r, requestId);
}

void to_json(nlohmann::json& j, const MsgPTaskTarget& t) {
    j["box"] = t.box;
    if (!t.targetId.empty()) {
        j["targetId"]     = t.targetId;
        j["sourceNodeId"] = t.sourceNodeId;
        j["atomicCode"]   = t.atomicCode;
        j["decision"]     = t.decision;
        j["rules"]        = t.rules;
        j["filtered"]     = t.filtered;
        if (t.filtered)
            j["filterReason"] = t.filterReason;
    }
    if (!t.attributes.empty())
        j["attributes"] = t.attributes;
    if (!t.texts.empty())
        j["texts"] = t.texts;
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
    JSON_OPT(j, t, targetId);
    JSON_OPT(j, t, sourceNodeId);
    JSON_OPT(j, t, atomicCode);
    JSON_OPT(j, t, decision);
    JSON_OPT(j, t, rules);
    JSON_OPT(j, t, filtered);
    JSON_OPT(j, t, filterReason);
    JSON_OPT(j, t, attributes);
    JSON_OPT(j, t, texts);
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
    JSON_OPT(j, v, schemaVersion);
    JSON_OPT(j, v, requestId);
    JSON_OPT(j, v, status);
    JSON_OPT(j, v, errorNodeId);
    JSON_OPT(j, v, errorSide);
    JSON_OPT(j, v, comparison);
    JSON_OPT(j, v, referencePicture);
    JSON_OPT(j, v, referenceTargetList);
    JSON_OPT(j, v, outputs);
    JSON_OPT(j, v, nodes);
    JSON_OPT(j, v, targetList);
    JSON_OPT(j, v, algorithmCode);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, fullPicture);
    JSON_OPT(j, v, areaList);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicSend::ResData& v) {
    j["algorithmCode"] = v.algorithmCode;
    j["timestamp"]     = v.timestamp;
    j["fullPicture"]   = v.fullPicture;
    if (v.schemaVersion < 2) {
        j["areaList"] = v.areaList;
    } else {
        j["schemaVersion"] = v.schemaVersion;
        j["requestId"]     = v.requestId;
        j["status"]        = v.status;
        j["outputs"]       = v.outputs;
        j["targetList"]    = v.targetList;
        if (!v.errorSide.empty())
            j["errorSide"] = v.errorSide;
        if (!v.comparison.nodeId.empty()) {
            j["comparison"]          = v.comparison;
            j["referencePicture"]    = v.referencePicture;
            j["referenceTargetList"] = v.referenceTargetList;
        }
        if (!v.errorNodeId.empty())
            j["errorNodeId"] = v.errorNodeId;
        if (!v.nodes.empty())
            j["nodes"] = v.nodes;
    }
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicSend::Output& v) {
    j["nodeId"]       = v.nodeId;
    j["name"]         = v.name;
    j["decision"]     = v.decision;
    j["targetIds"]    = v.targetIds;
    j["matchedCount"] = v.matchedCount;
    if (!v.reason.empty())
        j["reason"] = v.reason;
}

void from_json(const nlohmann::json& j, MsgPTaskDetectPicSend::Output& v) {
    j.at("nodeId").get_to(v.nodeId);
    j.at("name").get_to(v.name);
    j.at("decision").get_to(v.decision);
    j.at("targetIds").get_to(v.targetIds);
    j.at("matchedCount").get_to(v.matchedCount);
    JSON_OPT(j, v, reason);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicSend::Comparison& v) {
    j = {{"nodeId", v.nodeId}, {"featureType", v.featureType}, {"decision", v.decision}};
    if (v.hasScore)
        j["score"] = v.score;
    if (v.threshold >= 0)
        j["threshold"] = v.threshold;
}
void from_json(const nlohmann::json& j, MsgPTaskDetectPicSend::Comparison& v) {
    JSON_OPT(j, v, nodeId);
    JSON_OPT(j, v, featureType);
    JSON_OPT(j, v, decision);
    v.hasScore = j.contains("score") && j["score"].is_number();
    if (v.hasScore)
        j.at("score").get_to(v.score);
    JSON_OPT(j, v, threshold);
}

void to_json(nlohmann::json& j, const MsgPTaskDetectPicSend::NodeResult& v) {
    j["nodeId"]      = v.nodeId;
    j["actionId"]    = v.actionId;
    j["status"]      = v.status;
    j["inputCount"]  = v.inputCount;
    j["outputCount"] = v.outputCount;
    j["durationMs"]  = v.durationMs;
    if (!v.imageSide.empty())
        j["imageSide"] = v.imageSide;
}

void from_json(const nlohmann::json& j, MsgPTaskDetectPicSend::NodeResult& v) {
    j.at("nodeId").get_to(v.nodeId);
    j.at("actionId").get_to(v.actionId);
    j.at("status").get_to(v.status);
    j.at("inputCount").get_to(v.inputCount);
    j.at("outputCount").get_to(v.outputCount);
    j.at("durationMs").get_to(v.durationMs);
    JSON_OPT(j, v, imageSide);
}

}  // namespace cosmo
