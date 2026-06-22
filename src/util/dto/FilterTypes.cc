// FilterTypes — Filter/alarm types — MsgAiDetFrame, MsgFilterArea, MsgRecAlarm, etc.

#include "FilterTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgAiFilterFrame& f) {
    j["streamIndex"]  = f.streamIndex;
    j["index"]        = f.index;
    j["timestamp"]    = f.timestamp;
    j["actionFlowId"] = f.actionFlowId;
    j["bInputArea"]   = f.bInputArea;
    if (f.bInputArea) {
        j["areas"] = f.areas;
    } else if (!f.bInputArea) {
        j["targets"] = f.targets;
    } else {
        j["areas"] = f.areas;
    }
}

void from_json(const nlohmann::json& j, MsgAiFilterFrame& f) {
    JSON_OPT(j, f, streamIndex);
    JSON_OPT(j, f, index);
    JSON_OPT(j, f, timestamp);
    JSON_OPT(j, f, actionFlowId);
    JSON_OPT(j, f, bInputArea);
    JSON_OPT(j, f, areas);
    JSON_OPT(j, f, targets);
}

void to_json(nlohmann::json& j, const MsgLogicTestRecv& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["taskId"] = r.taskId;
    j["frame"]  = r.frame;
}

void from_json(const nlohmann::json& j, MsgLogicTestRecv& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    JSON_OPT(j, r, taskId);
    JSON_OPT(j, r, frame);
}

void to_json(nlohmann::json& j, const MsgLogicTestSend& s) {
    to_json(j, static_cast<const MsgSendHead&>(s));
    j["taskId"] = s.taskId;
    j["frame"]  = s.frame;
}

void from_json(const nlohmann::json& j, MsgLogicTestSend& s) {
    from_json(j, static_cast<MsgSendHead&>(s));
    JSON_OPT(j, s, taskId);
    JSON_OPT(j, s, frame);
}

void from_json(const nlohmann::json& j, MsgAiDetFrame& v) {
    JSON_OPT(j, v, streamIndex);
    JSON_OPT(j, v, index);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, targets);
}

void to_json(nlohmann::json& j, const MsgAiDetFrame& v) {
    j["streamIndex"] = v.streamIndex;
    j["index"]       = v.index;
    j["timestamp"]   = v.timestamp;
    j["targets"]     = v.targets;
}

void from_json(const nlohmann::json& j, MsgFilterArea& v) {
    JSON_OPT(j, v, areaId);
    JSON_OPT(j, v, areaName);
    JSON_OPT(j, v, bLogicResult);
    JSON_OPT(j, v, average);
    JSON_OPT(j, v, motionStatus);
    JSON_OPT(j, v, box);
    JSON_OPT(j, v, aiBox);
}

void to_json(nlohmann::json& j, const MsgFilterArea& v) {
    j["areaId"]       = v.areaId;
    j["areaName"]     = v.areaName;
    j["bLogicResult"] = v.bLogicResult;
    j["average"]      = v.average;
    j["motionStatus"] = v.motionStatus;
    j["box"]          = v.box;
    j["aiBox"]        = v.aiBox;
}

void from_json(const nlohmann::json& j, MsgRecAlarm& v) {
    JSON_OPT(j, v, streamIndex);
    JSON_OPT(j, v, index);
    JSON_OPT(j, v, filterAlarmInterval);
    JSON_OPT(j, v, filterTargetAlarmCount);
    JSON_OPT(j, v, filterTargetAlarmInterval);
    JSON_OPT(j, v, filterPosition);
    JSON_OPT(j, v, filterLlmReview);
    JSON_OPT(j, v, alarm);
    JSON_OPT(j, v, trackId);
    JSON_OPT(j, v, areaId);
}

void to_json(nlohmann::json& j, const MsgRecAlarm& v) {
    j["streamIndex"]               = v.streamIndex;
    j["index"]                     = v.index;
    j["filterAlarmInterval"]       = v.filterAlarmInterval;
    j["filterTargetAlarmCount"]    = v.filterTargetAlarmCount;
    j["filterTargetAlarmInterval"] = v.filterTargetAlarmInterval;
    j["filterPosition"]            = v.filterPosition;
    j["filterLlmReview"]           = v.filterLlmReview;
    j["alarm"]                     = v.alarm;
    j["trackId"]                   = v.trackId;
    j["areaId"]                    = v.areaId;
}

void from_json(const nlohmann::json& j, MsgRecSensitityTarget& v) {
    JSON_OPT(j, v, queIsFull);
    JSON_OPT(j, v, trackId);
    JSON_OPT(j, v, dem);
    JSON_OPT(j, v, rsts);
    JSON_OPT(j, v, sensitity);
    JSON_OPT(j, v, expertSensitity);
}

void to_json(nlohmann::json& j, const MsgRecSensitityTarget& v) {
    j["queIsFull"]       = v.queIsFull;
    j["trackId"]         = v.trackId;
    j["dem"]             = v.dem;
    j["rsts"]            = v.rsts;
    j["sensitity"]       = v.sensitity;
    j["expertSensitity"] = v.expertSensitity;
}

void from_json(const nlohmann::json& j, MsgRecArea& v) {
    JSON_OPT(j, v, areaId);
    JSON_OPT(j, v, targets);
}

void to_json(nlohmann::json& j, const MsgRecArea& v) {
    j["areaId"]  = v.areaId;
    j["targets"] = v.targets;
}

void from_json(const nlohmann::json& j, MsgRecSensitity& v) {
    JSON_OPT(j, v, streamIndex);
    JSON_OPT(j, v, index);
    JSON_OPT(j, v, areas);
}

void to_json(nlohmann::json& j, const MsgRecSensitity& v) {
    j["streamIndex"] = v.streamIndex;
    j["index"]       = v.index;
    j["areas"]       = v.areas;
}

void from_json(const nlohmann::json& j, MsgRecPosSaveSensitityTarget& v) {
    JSON_OPT(j, v, trackId);
    JSON_OPT(j, v, behaviorDetected);
    JSON_OPT(j, v, duration);
    JSON_OPT(j, v, rsts);
    JSON_OPT(j, v, box);
    JSON_OPT(j, v, aiBox);
}

void to_json(nlohmann::json& j, const MsgRecPosSaveSensitityTarget& v) {
    j["trackId"]          = v.trackId;
    j["behaviorDetected"] = v.behaviorDetected;
    j["duration"]         = v.duration;
    j["rsts"]             = v.rsts;
    j["box"]              = v.box;
    j["aiBox"]            = v.aiBox;
}

void from_json(const nlohmann::json& j, MsgRecPosSaveSensitity& v) {
    JSON_OPT(j, v, streamIndex);
    JSON_OPT(j, v, index);
    JSON_OPT(j, v, timestamp);
    JSON_OPT(j, v, targets);
}

void to_json(nlohmann::json& j, const MsgRecPosSaveSensitity& v) {
    j["streamIndex"] = v.streamIndex;
    j["index"]       = v.index;
    j["timestamp"]   = v.timestamp;
    j["targets"]     = v.targets;
}

}  // namespace cosmo
