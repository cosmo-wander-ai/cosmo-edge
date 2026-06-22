// AlgorithmMsgTypes — Algorithm types — ActionBase, ActionAlg, ActionAlgPtr.

#include "AlgorithmMsgTypes.h"

#include <nlohmann/json.hpp>

#include "util/JsonFieldOpt.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const ActionBase& b) {
    j["flowActionId"]    = b.flowActionId;
    j["preFlowActionId"] = b.preFlowActionId;
    j["initFps"]         = b.initFps;
}

void from_json(const nlohmann::json& j, ActionBase& b) {
    j.at("flowActionId").get_to(b.flowActionId);        // mandatory
    j.at("preFlowActionId").get_to(b.preFlowActionId);  // mandatory
    JSON_OPT(j, b, initFps);
}

void to_json(nlohmann::json& j, const ActionNode& n) {
    to_json(j, static_cast<const ActionBase&>(n));
    j["actionId"]     = n.actionId;
    j["actionName"]   = n.actionName;
    j["configObject"] = n.configObject;
}

void from_json(const nlohmann::json& j, ActionNode& n) {
    from_json(j, static_cast<ActionBase&>(n));
    j.at("actionId").get_to(n.actionId);  // mandatory
    JSON_OPT(j, n, actionName);
    JSON_OPT(j, n, configObject);
}

void to_json(nlohmann::json& j, const ActionAlg& a) {
    j["algorithmName"]       = a.algorithmName;
    j["algorithmCode"]       = a.algorithmCode;
    j["algorithmUpdateTime"] = a.algorithmUpdateTime;
    j["workFlow"]            = a.workFlow;
    j["algorithmCheckSum"]   = a.algorithmCheckSum;
    j["algorithmMinFps"]     = a.algorithmMinFps;
    j["category"]            = a.category;
}

void from_json(const nlohmann::json& j, ActionAlg& a) {
    j.at("algorithmName").get_to(a.algorithmName);              // mandatory
    j.at("algorithmCode").get_to(a.algorithmCode);              // mandatory
    j.at("algorithmUpdateTime").get_to(a.algorithmUpdateTime);  // mandatory
    j.at("workFlow").get_to(a.workFlow);                        // mandatory
    JSON_OPT(j, a, algorithmCheckSum);
    JSON_OPT(j, a, algorithmMinFps);
    JSON_OPT(j, a, category);
}

void to_json(nlohmann::json& j, const MsgLoadLocalAlgorithmActionRecv& r) {
    to_json(j, static_cast<const MsgRecvHead&>(r));
    j["fileName"]       = r.fileName;
    j["action"]         = r.action;
    j["taskActionType"] = static_cast<int>(r.taskActionType);
}

void from_json(const nlohmann::json& j, MsgLoadLocalAlgorithmActionRecv& r) {
    from_json(j, static_cast<MsgRecvHead&>(r));
    JSON_OPT(j, r, fileName);
    JSON_OPT(j, r, action);
    if (auto it = j.find("taskActionType"); it != j.end() && !it->is_null())
        r.taskActionType = static_cast<MsgTaskActionType>(it->get<int>());
}

void from_json(const nlohmann::json& j, MvActionConfigObject& v) {
    JSON_OPT(j, v, params);
    JSON_OPT(j, v, condition);
}

void to_json(nlohmann::json& j, const MvActionConfigObject& v) {
    j["params"]    = v.params;
    j["condition"] = v.condition;
}

void from_json(const nlohmann::json& j, MsgAlgorithmMetaDataRegion& v) {
    JSON_OPT(j, v, heads);
}

void to_json(nlohmann::json& j, const MsgAlgorithmMetaDataRegion& v) {
    j["heads"] = v.heads;
}

void from_json(const nlohmann::json& j, MsgAlgorithmMetaData& v) {
    JSON_OPT(j, v, params);
    JSON_OPT(j, v, region);
    JSON_OPT(j, v, shieldedRegion);
    JSON_OPT(j, v, regionType);
    JSON_OPT(j, v, scheduleSupport);
    JSON_OPT(j, v, defaultFullScreen);
    JSON_OPT(j, v, maxAreaCount);
}

void to_json(nlohmann::json& j, const MsgAlgorithmMetaData& v) {
    j["params"]            = v.params;
    j["region"]            = v.region;
    j["shieldedRegion"]    = v.shieldedRegion;
    j["regionType"]        = v.regionType;
    j["scheduleSupport"]   = v.scheduleSupport;
    j["defaultFullScreen"] = v.defaultFullScreen;
    j["maxAreaCount"]      = v.maxAreaCount;
}

}  // namespace cosmo
