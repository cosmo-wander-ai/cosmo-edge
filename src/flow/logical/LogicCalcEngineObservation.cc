// Pure logical observations retain missing inputs for result accumulation.

#include <algorithm>
#include <cmath>

#include "flow/logical/LogicCalcEngine.h"
#include "util/Keys.h"

namespace cosmo {

LogicCalcEngine::LogicCalcEngine(const std::string& logTag, ParamLimitValueFn paramFn,
                                 ValueIsIncludeFn includeFn)
    : log_tag_(std::move(logTag)), param_fn_(std::move(paramFn)), include_fn_(std::move(includeFn)) {}

// --- Pure function: independent of parameter storage ---

bool LogicCalcEngine::GetAiOutValue(const AiDetectRstEl& target, const std::string& label, float& value) {
    if (label == target.confidence.label) {
        value = target.confidence.confidence;
        return true;
    }

    auto it = std::find_if(target.classifyRst.begin(), target.classifyRst.end(),
                           [&label](const auto& classEl) { return label == classEl.label; });
    if (it != target.classifyRst.end()) {
        value = it->confidence;
        return true;
    }

    if (target.relatedEl.bActive) {
        auto itRelated =
            std::find_if(target.relatedEl.classifyRst.begin(), target.relatedEl.classifyRst.end(),
                         [&label](const auto& relatedEl) { return label == relatedEl.label; });
        if (itRelated != target.relatedEl.classifyRst.end()) {
            value = itRelated->confidence;
            return true;
        }
    }

    return false;
}

bool LogicCalcEngine::GetAiOutValueAttr(const AiDetectRstEl& target, const std::string& label,
                                        std::string& value) {
    auto it = std::find_if(target.attrRst.begin(), target.attrRst.end(),
                           [&label](const auto& classEl) { return label == classEl.category; });
    if (it != target.attrRst.end()) {
        value = it->label;
        return true;
    }
    return false;
}

std::optional<bool> LogicCalcEngine::Observe(const AiDetectRstEl& target, LogicCalc& logic,
                                             const ValueIsConfiguredFn& configured) const {
    if (logic.type == LogicType::Include || logic.type == LogicType::NonInclude) {
        if (!configured || !configured(logic.keyL.ToString())) {
            return std::nullopt;
        }
        const bool included = include_fn_(logic.keyL, logic.keyR);
        return logic.type == LogicType::Include ? included : !included;
    }
    if (!logic.list.empty()) {
        // NOR is a reserved legacy pass-through, not an implemented negation operator.
        if (logic.type != LogicType::AND && logic.type != LogicType::OR) {
            return std::nullopt;
        }
        bool unknown = false;
        for (auto& child : logic.list) {
            const auto result = Observe(target, child, configured);
            if (!result) {
                unknown = true;
            } else if ((logic.type == LogicType::OR && *result) ||
                       (logic.type == LogicType::AND && !*result)) {
                return *result;
            }
        }
        if (unknown) {
            return std::nullopt;
        }
        return logic.type == LogicType::AND;
    }
    if (logic.type < LogicType::Equal || logic.type > LogicType::LE) {
        return std::nullopt;
    }
    if (logic.keyLElements.size() != 3 || logic.keyRElements.size() != 3) {
        return std::nullopt;
    }
    const bool parameter_on_left = logic.keyLElements[0] == key::AI_PARAM;
    const auto& ai               = parameter_on_left ? logic.keyRElements : logic.keyLElements;
    const auto& parameter        = parameter_on_left ? logic.keyLElements : logic.keyRElements;
    float left = 0.0f, right = 0.0f;
    float ai_value = 0.0f, parameter_value = 0.0f;
    if (GetAiOutValue(target, ai[1], ai_value) && param_fn_(parameter[1], parameter_value)) {
        left  = parameter_on_left ? parameter_value : ai_value;
        right = parameter_on_left ? ai_value : parameter_value;
        if (!std::isfinite(left) || !std::isfinite(right)) {
            return std::nullopt;
        }
        switch (logic.type) {
            case LogicType::Equal:
                return left == right;
            case LogicType::NEQ:
                return left != right;
            case LogicType::Greater:
                return left > right;
            case LogicType::GE:
                return left >= right;
            case LogicType::Less:
                return left < right;
            case LogicType::LE:
                return left <= right;
            default:
                return std::nullopt;
        }
    }
    if (logic.type == LogicType::Equal || logic.type == LogicType::NEQ) {
        std::string value;
        if (GetAiOutValueAttr(target, ai[2], value)) {
            return logic.type == LogicType::Equal ? value == parameter[2] : value != parameter[2];
        }
    }
    return std::nullopt;
}

}  // namespace cosmo
