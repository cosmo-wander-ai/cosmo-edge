#include "flow/logical/PictureRule.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "util/StringUtil.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {
namespace {

    std::optional<double> Number(const std::string& text) {
        if (text.empty())
            return {};
        char* end          = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end || !std::isfinite(value))
            return {};
        return value;
    }

    std::optional<std::string> Operand(const std::string& key, const AlgData& data,
                                       const AiDetectRstEl* target,
                                       const std::map<std::string, std::string>& params) {
        const auto param = params.find(key);
        if (param != params.end())
            return param->second;
        if (key == "picture.count" || key == "picture.matchedCount") {
            size_t count = 0;
            if (data.chanDataDetect.detRet)
                for (const auto& item : data.chanDataDetect.detRet->targets) {
                    if (item.bFilter)
                        continue;
                    if (key == "picture.matchedCount" && data.bHaveLogic) {
                        const auto result = data.pictureDecisions.find(item.targetId);
                        if (result == data.pictureDecisions.end() || result->second == "unknown")
                            return {};
                    }
                    if (key == "picture.count" || !data.bHaveLogic || item.bLogicResult)
                        ++count;
                }
            return std::to_string(count);
        }
        if (key.rfind("node.", 0) == 0) {
            const auto node = key.substr(5);
            const auto it   = data.pictureRules.find(node);
            if (it == data.pictureRules.end())
                return {};
            auto value = it->second.find(target ? target->targetId : "image");
            if (value == it->second.end() || value->second == "unknown")
                return {};
            return value->second == "matched" ? "1" : "0";
        }
        if (key.rfind("aiParam.attr.", 0) == 0)
            return key.substr(13);
        if (target) {
            if (key == "match.matched") {
                if (target->matchInfo.setPicCount <= 0)
                    return {};
                return target->matchInfo.matched ? "1" : "0";
            }
            if (key == "match.score") {
                if (target->matchInfo.setPicCount <= 0)
                    return {};
                return std::to_string(target->matchInfo.match_degree);
            }
            if (key == "ocr.text") {
                if (target->ocrRst.empty())
                    return {};
                return target->ocrRst.front().value;
            }
            if (key.rfind("aiOut.attr.", 0) == 0) {
                const auto category = key.substr(11);
                for (const auto& attr : target->attrRst)
                    if (attr.category == category)
                        return attr.label;
                return {};
            }
            if (key.rfind("aiOut.", 0) == 0) {
                const auto end = key.rfind('.');
                if (end <= 6)
                    return {};
                const auto label = key.substr(6, end - 6);
                if (target->confidence.label == label)
                    return std::to_string(target->confidence.confidence);
                for (const auto& value : target->classifyRst)
                    if (value.label == label)
                        return std::to_string(value.confidence);
                return {};
            }
        }
        if (key.rfind("aiOut.", 0) == 0 || key.rfind("aiParam.", 0) == 0 || key.rfind("custom.", 0) == 0 ||
            key.rfind("match.", 0) == 0 || key == "ocr.text")
            return {};
        return key;
    }

    std::string Decision(const std::optional<bool>& value) {
        return !value ? "unknown" : (*value ? "matched" : "not_matched");
    }

}  // namespace

std::optional<bool> EvaluatePictureRule(const LogicCalc& rule, const AlgData& data,
                                        const AiDetectRstEl* target,
                                        const std::map<std::string, std::string>& params) {
    if (rule.type == LogicType::NOR) {
        if (rule.list.size() != 1)
            return {};
        auto value = EvaluatePictureRule(rule.list.front(), data, target, params);
        return value ? std::optional<bool>(!*value) : std::nullopt;
    }
    if (rule.type == LogicType::AND || rule.type == LogicType::OR) {
        if (rule.list.empty())
            return {};
        bool unknown = false;
        for (const auto& child : rule.list) {
            const auto value = EvaluatePictureRule(child, data, target, params);
            if (!value)
                unknown = true;
            else if (rule.type == LogicType::AND && !*value)
                return false;
            else if (rule.type == LogicType::OR && *value)
                return true;
        }
        if (unknown)
            return {};
        return rule.type == LogicType::AND;
    }
    auto left = Operand(rule.keyL.ToString(), data, target, params);
    if (!left)
        return {};
    if (rule.type == LogicType::EMPTY)
        return left->empty();
    if (rule.type == LogicType::NonEmpty)
        return !left->empty();
    auto right = Operand(rule.keyR.ToString(), data, target, params);
    if (!right)
        return {};
    if (rule.type == LogicType::Include || rule.type == LogicType::NonInclude) {
        bool contains = false;
        for (const auto& part : util::Split(*left, ","))
            if (util::Trim(part) == *right)
                contains = true;
        return rule.type == LogicType::Include ? contains : !contains;
    }
    const auto l = Number(*left), r = Number(*right);
    if (rule.type == LogicType::Equal)
        return l && r ? *l == *r : *left == *right;
    if (rule.type == LogicType::NEQ)
        return l && r ? *l != *r : *left != *right;
    if (!l || !r)
        return {};
    switch (rule.type) {
        case LogicType::Greater:
            return *l > *r;
        case LogicType::GE:
            return *l >= *r;
        case LogicType::Less:
            return *l < *r;
        case LogicType::LE:
            return *l <= *r;
        default:
            return {};
    }
}

PictureRuleAction::PictureRuleAction(const std::string& taskId, ActionNode& action)
    : PActionBase(action, taskId), condition_(action.configObject.condition) {
    for (const auto& param : action.configObject.params) {
        const auto key   = param.key.ToString();
        const auto parts = util::Split(key, ".");
        if (parts.size() == 4 && parts[0] == "filter")
            filter_labels_.insert(std::string(parts[1]));
    }
}

bool PictureRuleAction::SetParam(const std::string&, std::vector<MsgDynamicKeyValue>& params) {
    params_.clear();
    for (const auto& param : params) {
        const auto key   = param.key.ToString();
        const auto parts = util::Split(key, ".");
        if (parts.size() == 4 && parts[0] == "filter" && !filter_labels_.empty() &&
            !filter_labels_.count(std::string(parts[1])))
            continue;
        params_[key] = param.value.ToString();
    }
    for (const auto& [key, value] : params_) {
        if (key.rfind("filter.", 0) != 0)
            continue;
        const auto parts = util::Split(key, ".");
        if (parts.size() != 4 || (parts[2] != "confidence" && parts[2] != "size" && parts[2] != "side") ||
            (parts[3] != "min" && parts[3] != "max") || !Number(value))
            return false;
        if (*Number(value) < 0 || (parts[2] == "confidence" && *Number(value) > 1))
            return false;
    }
    return true;
}

util::ErrorEnum PictureRuleAction::HandPic(AlgDataPtr data) {
    if (!data)
        return util::ErrorEnum::FlowDataInvalid;
    if (GetActionId() == PAOutput_Code)
        return util::ErrorEnum::Success;
    if (GetActionId() == PAImageJudgment_Code || GetActionId() == PABranch_Code) {
        auto result           = EvaluatePictureRule(condition_, *data, nullptr, params_);
        data->pictureDecision = Decision(result);
        data->pictureRules[GetFlowActionId()]["image"] = data->pictureDecision;
        if (GetActionId() == PABranch_Code)
            data->pictureBranch = result && *result;
        return util::ErrorEnum::Success;
    }
    if (!data->chanDataDetect.detRet)
        return util::ErrorEnum::FlowDataNull;
    for (auto& target : data->chanDataDetect.detRet->targets) {
        if (target.bFilter)
            continue;
        if (GetActionId() == PAFilter_Code) {
            bool has_filters = false, selected = false;
            for (const auto& [key, text] : params_) {
                if (key.rfind("filter.", 0) != 0)
                    continue;
                has_filters      = true;
                const auto parts = util::Split(key, ".");
                if (parts[1] != target.confidence.label)
                    continue;
                selected         = true;
                const auto limit = Number(text);
                const double actual =
                    parts[2] == "confidence"
                        ? target.confidence.confidence
                        : (parts[2] == "side" ? std::min(target.box.width, target.box.height)
                                              : static_cast<double>(target.box.width) * target.box.height);
                if ((parts[3] == "min" && actual < *limit) || (parts[3] == "max" && actual > *limit)) {
                    target.bFilter    = true;
                    target.filterDesc = key;
                }
            }
            if (has_filters && !selected) {
                target.bFilter    = true;
                target.filterDesc = "label_not_selected";
            }
        } else {
            const auto result   = EvaluatePictureRule(condition_, *data, &target, params_);
            const auto decision = Decision(result);
            data->pictureRules[GetFlowActionId()][target.targetId] = decision;
            data->pictureDecisions[target.targetId]                = decision;
            target.bLogicResult                                    = result && *result;
            data->bHaveLogic                                       = true;
        }
    }
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
