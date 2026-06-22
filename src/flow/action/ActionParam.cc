// ActionParam — Action Param implementation.

#include "ActionParam.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const ActionParamBase& v) {
    j["flowActionId"] = v.flowActionId;
}

void from_json(const nlohmann::json& j, ActionParamBase& v) {
    j.at("flowActionId").get_to(v.flowActionId);
}

void to_json(nlohmann::json& j, const ActionParam& v) {
    to_json(j, static_cast<const ActionParamBase&>(v));
    j["params"] = v.params;
}

void from_json(const nlohmann::json& j, ActionParam& v) {
    from_json(j, static_cast<ActionParamBase&>(v));
    if (j.contains("params") && !j["params"].is_null())
        j.at("params").get_to(v.params);
}

void from_json(const nlohmann::json& j, TaskParam& v) {
    if (j.contains("actions") && !j["actions"].is_null())
        j.at("actions").get_to(v.actions);
}

void to_json(nlohmann::json& j, const TaskParam& v) {
    j["actions"] = v.actions;
}

}  // namespace cosmo
