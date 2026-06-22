// LinkAgeBaseCommon — Link Age Base Common implementation.

#include "linkage/LinkAgeBaseCommon.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo::linkage {
void to_json(nlohmann::json& j, const LinkAgeParamNode& v) {
    to_json(j, static_cast<const ActionBase&>(v));
    j["actionId"]     = v.action_id;
    j["actionName"]   = v.action_name;
    j["configObject"] = v.config_object;
}

void from_json(const nlohmann::json& j, LinkAgeParamNode& v) {
    from_json(j, static_cast<ActionBase&>(v));
    j.at("actionId").get_to(v.action_id);
    if (j.contains("actionName") && !j["actionName"].is_null())
        j.at("actionName").get_to(v.action_name);
    if (j.contains("configObject") && !j["configObject"].is_null())
        j.at("configObject").get_to(v.config_object);
}

void from_json(const nlohmann::json& j, LinkageStrategyWorkflow& v) {
    if (j.contains("workflow") && !j["workflow"].is_null())
        j.at("workflow").get_to(v.workflow);
}

void to_json(nlohmann::json& j, const LinkageStrategyWorkflow& v) {
    j["workflow"] = v.workflow;
}

}  // namespace cosmo::linkage
