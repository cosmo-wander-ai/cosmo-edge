#pragma once

#include <string>
#include <utility>
#include <vector>

#include "flow/detect/NativeFaceActionGuard.h"
#include "flow/detect/PersonTemplateContract.h"
#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo::person_template_contract {

// Use the same projection for a draft candidate and the real execution graph.
// The candidate changes only the projected value; it never mutates the draft.
inline std::vector<Node> ProjectActionGraph(const std::vector<ActionNode>& actions) {
    std::vector<Node> nodes;
    nodes.reserve(actions.size());
    for (const auto& action : actions) {
        Node node{
            action.flowActionId, action.preFlowActionId, action.actionId, action.atomicCode, "", "", true};
        for (const auto& param : action.configObject.params) {
            const auto key   = param.key.ToString();
            const auto value = param.value.ToString();
            if (key == "alarmProperty") {
                if (!node.alarm_property.empty())
                    node.valid = false;
                node.alarm_property = value;
            }
            if (key == "custParam.alarmType") {
                if (!node.alarm_type.empty())
                    node.valid = false;
                node.alarm_type = value;
            }
            if (key == "atomicCode" && value != action.atomicCode)
                node.valid = false;
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

inline bool MatchesActionGraph(const std::vector<ActionNode>& actions, const std::string& category) {
    return Matches(ProjectActionGraph(actions), category);
}

inline bool MatchesCandidateGraph(const std::vector<ActionNode>& actions, const std::string& category,
                                  const std::string& target_node) {
    if (target_node.empty() || target_node == "-1")
        return false;
    auto candidate    = actions;
    unsigned selected = 0;
    for (auto& action : candidate) {
        if (action.flowActionId == target_node && action.actionId == "AA_00001") {
            if (!native_face::ConsistentStrictAction("1001003", action.atomicCode, action) ||
                !native_face::ConsistentStrictAction("9275710", action.atomicCode, action))
                return false;
            unsigned model_params = 0;
            for (auto& param : action.configObject.params) {
                if (native_face::IsModelCodeKey(param.key.ToString())) {
                    param.value = "1001003";
                    ++model_params;
                }
            }
            if (model_params > 1)
                return false;
            if (model_params == 0) {
                MsgDynamicKeyValue param;
                param.key   = "atomicCode";
                param.value = "1001003";
                action.configObject.params.push_back(param);
            }
            action.atomicCode = "1001003";
            ++selected;
        }
    }
    return selected == 1 && MatchesActionGraph(candidate, category);
}
}  // namespace cosmo::person_template_contract
