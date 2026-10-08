#include "util/dto/PictureWorkflow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "util/dto/ActionCodes.h"

namespace cosmo {
namespace {
    bool ValidRule(const LogicCalc& rule, const std::set<std::string>& ancestors, unsigned depth = 0) {
        if (depth > 32 || !IsValidLogicType(rule.type))
            return false;
        if (rule.type == LogicType::AND || rule.type == LogicType::OR || rule.type == LogicType::NOR) {
            if (rule.list.empty() || (rule.type == LogicType::NOR && rule.list.size() != 1))
                return false;
            return std::all_of(rule.list.begin(), rule.list.end(),
                               [&](const auto& child) { return ValidRule(child, ancestors, depth + 1); });
        }
        if (!rule.list.empty() || rule.keyL.empty())
            return false;
        for (const auto& operand : {rule.keyL.ToString(), rule.keyR.ToString()})
            if (operand.rfind("node.", 0) == 0 && !ancestors.count(operand.substr(5)))
                return false;
        return true;
    }
}  // namespace

bool OrderWorkflow(std::vector<ActionNode>& nodes, std::string& error) {
    std::map<std::string, size_t> ids;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& id = nodes[i].flowActionId;
        if (id.empty() || id == "-1" || !ids.emplace(id, i).second) {
            error = "Invalid or duplicate flowActionId: " + id;
            return false;
        }
    }
    std::vector<ActionNode> ordered;
    std::set<std::string> visited;
    while (ordered.size() < nodes.size()) {
        const auto before = ordered.size();
        for (const auto& node : nodes) {
            if (visited.count(node.flowActionId))
                continue;
            if (node.preFlowActionId != "-1" && !ids.count(node.preFlowActionId)) {
                error = node.flowActionId + ": missing parent " + node.preFlowActionId;
                return false;
            }
            if (node.preFlowActionId == "-1" || visited.count(node.preFlowActionId)) {
                ordered.push_back(node);
                visited.insert(node.flowActionId);
            }
        }
        if (before == ordered.size()) {
            error = "Workflow contains a cycle";
            return false;
        }
    }
    nodes = std::move(ordered);
    return true;
}

bool ValidatePictureParams(const std::vector<MsgDynamicKeyValue>& params) {
    std::map<std::string, double> limits;
    for (const auto& param : params) {
        const auto key = param.key.ToString(), value = param.value.ToString();
        const bool confidence = key.rfind("aiParam.", 0) == 0 && key.size() >= 11 &&
                                key.compare(key.size() - 11, 11, ".confidence") == 0;
        const bool filter = key.rfind("filter.", 0) == 0;
        if (confidence || filter || key == "param.limitScore") {
            char* end         = nullptr;
            const auto number = std::strtod(value.c_str(), &end);
            if (value.empty() || *end || !std::isfinite(number) || number < 0)
                return false;
            if ((confidence || key.find(".confidence.") != std::string::npos) && number > 1)
                return false;
            if (key == "param.limitScore" && number > 100)
                return false;
            limits[key] = number;
        }
        if (key == "output.targets" && value != "all" && value != "matched")
            return false;
        if (key == "match.libraryType" && value != "face" && value != "body")
            return false;
        if (key == "match.mode" && value != "matched" && value != "unmatched")
            return false;
    }
    for (const auto& [key, value] : limits) {
        if (key.size() < 4 || key.compare(key.size() - 4, 4, ".min") != 0)
            continue;
        const auto upper = limits.find(key.substr(0, key.size() - 4) + ".max");
        if (upper != limits.end() && value > upper->second)
            return false;
    }
    return true;
}

bool IsPictureModelAction(const std::string& id) {
    return id == PADetect_Code || id == PAClassify_Code || id == PALandmark_Code || id == PARecognizer_Code ||
           id == PAOcr_Code || id == PDADino_Code || id == PDASam_Code || id == PDAQwen3VL_Code;
}

bool IsPictureAction(const std::string& id) {
    return IsPictureModelAction(id) || id == PAFilter_Code || id == PALogicalJudgment_Code ||
           id == PAOutput_Code || id == PAMatch_Code || id == PAImageJudgment_Code || id == PABranch_Code;
}

bool ValidatePictureWorkflow(std::vector<ActionNode>& nodes, std::string& error) {
    if (!OrderWorkflow(nodes, error))
        return false;
    struct Capabilities {
        bool targets{false};
        bool features{false};
    };
    std::map<std::string, Capabilities> outputs;
    std::map<std::string, std::string> actions;
    std::map<std::string, std::set<std::string>> ruleAncestors;
    for (const auto& node : nodes) {
        const auto& id = node.actionId;
        auto fail      = [&](const std::string& reason) {
            error = node.flowActionId + ": " + reason;
            return false;
        };
        if (!ValidatePictureParams(node.configObject.params))
            return fail("invalid picture parameters");
        if (!IsPictureAction(id))
            return fail("action does not support single images: " + id);
        auto caps      = outputs[node.preFlowActionId];
        auto ancestors = ruleAncestors[node.preFlowActionId];
        if (IsPictureModelAction(id) && node.atomicCode.empty() &&
            std::none_of(node.configObject.params.begin(), node.configObject.params.end(),
                         [](const auto& p) { return p.key == "atomicCode" && !p.value.empty(); }))
            return fail("missing model atomicCode");
        if (actions[node.preFlowActionId] == PAOutput_Code)
            return fail("result output must be terminal");
        bool whole_image = false, target_input = false;
        for (const auto& param : node.configObject.params) {
            const auto key = param.key.ToString();
            if (key == "inputType" && param.value == "image")
                whole_image = true;
            if (key == "inputType" && param.value == "targets")
                target_input = true;
            if (key.rfind("filter.", 0) == 0 && key.find(".motion.") != std::string::npos)
                return fail("motion filters require video history");
        }
        if (id == PDAQwen3VL_Code && target_input && !caps.targets)
            return fail("requires upstream targets");
        if (id == PADetect_Code || id == PDADino_Code || id == PDASam_Code || id == PDAQwen3VL_Code ||
            (id == PAClassify_Code && whole_image))
            caps.targets = true;
        if ((id == PAClassify_Code || id == PALandmark_Code || id == PARecognizer_Code || id == PAOcr_Code ||
             id == PAFilter_Code || id == PALogicalJudgment_Code) &&
            !caps.targets)
            return fail("requires upstream targets");
        if (id == PAMatch_Code && !caps.features)
            return fail("requires upstream feature extraction");
        if (id == PARecognizer_Code)
            caps.features = true;
        if ((id == PALogicalJudgment_Code || id == PAImageJudgment_Code || id == PABranch_Code) &&
            !ValidRule(node.configObject.condition, ancestors))
            return fail("invalid rule or reference to a non-ancestor result");
        if (id == PALogicalJudgment_Code || id == PAImageJudgment_Code || id == PABranch_Code ||
            id == PAMatch_Code || id == PDAQwen3VL_Code)
            ancestors.insert(node.flowActionId);
        ruleAncestors[node.flowActionId] = std::move(ancestors);
        outputs[node.flowActionId]       = caps;
        actions[node.flowActionId]       = id;
    }
    return true;
}

}  // namespace cosmo
