// Common logic calculation engine — shared by streaming and image

#include "flow/logical/LogicCalcEngine.h"

#include <algorithm>

#include "util/Keys.h"
#include "util/Log.h"

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

// --- Functions requiring parameter callback ---

bool LogicCalcEngine::GetLeftRightValue(const AiDetectRstEl& target, LogicCalc& logic, float& lValue,
                                        float& rValue) const {
    if (3 != logic.keyLElements.size()) {
        LOG_WARN("[{}] keyL:{} type:{}", log_tag_, logic.keyL, logic.type);
        return false;
    }
    if (3 != logic.keyRElements.size()) {
        LOG_WARN("[{}] keyR:{}", log_tag_, logic.keyR);
        return false;
    }

    std::string keyLLabel = logic.keyLElements[1];
    std::string keyRLabel = logic.keyRElements[1];
    std::string keyLType  = logic.keyLElements[0];

    std::string aiLabel    = keyLLabel;
    std::string paramLabel = keyRLabel;
    if (key::AI_PARAM == keyLType) {
        aiLabel    = keyRLabel;
        paramLabel = keyLLabel;
        if (!GetAiOutValue(target, aiLabel, rValue))
            return false;
        if (!param_fn_(paramLabel, lValue))
            return false;
    } else {
        if (!GetAiOutValue(target, aiLabel, lValue))
            return false;
        if (!param_fn_(paramLabel, rValue))
            return false;
    }
    return true;
}

bool LogicCalcEngine::GetLeftRightValueAttr(const AiDetectRstEl& target, LogicCalc& logic,
                                            std::string& lValue, std::string& rValue) const {
    if (3 != logic.keyLElements.size()) {
        LOG_WARN("[{}] keyL:{} type:{}", log_tag_, logic.keyL, logic.type);
        return false;
    }
    if (3 != logic.keyRElements.size()) {
        LOG_WARN("[{}] keyR:{}", log_tag_, logic.keyR);
        return false;
    }
    std::string keyLLabel = logic.keyLElements[2];
    std::string keyRLabel = logic.keyRElements[2];
    std::string keyLType  = logic.keyLElements[0];

    std::string aiLabel    = keyLLabel;
    std::string paramLabel = keyRLabel;

    if (key::AI_PARAM == keyLType) {
        aiLabel    = keyRLabel;
        paramLabel = keyLLabel;
        if (!GetAiOutValueAttr(target, aiLabel, rValue))
            return false;
        lValue = paramLabel;
    } else {
        if (!GetAiOutValueAttr(target, aiLabel, lValue))
            return false;
        rValue = paramLabel;
    }
    return true;
}

bool LogicCalcEngine::CalcArithmeticOperationAttr(const AiDetectRstEl& target, LogicCalc& logic) const {
    std::string lValue;
    std::string rValue;
    if (!GetLeftRightValueAttr(target, logic, lValue, rValue))
        return false;

    switch (logic.type) {
        case LogicType::Equal:
            return lValue == rValue;
        case LogicType::NEQ:
            return lValue != rValue;
        default:
            LOG_WARN("[{}] LogicType {} Error", log_tag_, logic.type);
            break;
    }
    return false;
}

bool LogicCalcEngine::CalcArithmeticOperation(const AiDetectRstEl& target, LogicCalc& logic) const {
    float lValue = 0.0;
    float rValue = 0.0;
    auto ret     = GetLeftRightValue(target, logic, lValue, rValue);
    if (!ret) {
        return CalcArithmeticOperationAttr(target, logic);
    }
    switch (logic.type) {
        case LogicType::Equal:
            return lValue == rValue;
        case LogicType::NEQ:
            return lValue != rValue;
        case LogicType::Greater:
            return lValue > rValue;
        case LogicType::GE:
            return lValue >= rValue;
        case LogicType::Less:
            return lValue < rValue;
        case LogicType::LE:
            return lValue <= rValue;
        default:
            LOG_WARN("[{}] LogicType {} Error", log_tag_, logic.type);
            break;
    }
    return false;
}

bool LogicCalcEngine::CalcLogicOperation(const AiDetectRstEl& target, LogicCalc& logic) const {
    bool resInit = false;
    if (LogicType::OR == logic.type) {
        resInit = false;
        if (logic.list.size() < 1) {
            LOG_WARN("[{}] LOGIC OR But The Queue Size is {}", log_tag_, logic.list.size());
            return false;
        }
    } else if (LogicType::AND == logic.type) {
        resInit = true;
        if (logic.list.size() < 1) {
            LOG_WARN("[{}] LOGIC AND But The Queue Size is {}", log_tag_, logic.list.size());
            return false;
        }
    } else if (LogicType::NOR == logic.type) {
        resInit = false;
        if (logic.list.size() != 1) {
            LOG_WARN("[{}] LOGIC NOR But The Queue Size is {}", log_tag_, logic.list.size());
            return false;
        }
    } else {
        LOG_WARN("[{}] LOGIC Type Error:{}", log_tag_, logic.type);
        return false;
    }

    for (auto& logicItem : logic.list) {
        switch (logic.type) {
            case LogicType::OR: {
                resInit = resInit || GetLogicResult(target, logicItem);
            } break;
            case LogicType::AND: {
                resInit = resInit && GetLogicResult(target, logicItem);
            } break;
            case LogicType::NOR: {
                return GetLogicResult(target, logicItem);
            } break;
            default: {
                LOG_WARN("[{}] LogicType Error:{}", log_tag_, logic.type);
            }
                return false;
        }
    }

    return resInit;
}

bool LogicCalcEngine::CalcIncludeOperation(LogicCalc& logic) const {
    switch (logic.type) {
        case LogicType::Include:
            return include_fn_(logic.keyL, logic.keyR);
        case LogicType::NonInclude:
            return !include_fn_(logic.keyL, logic.keyR);
        default:
            LOG_WARN("[{}] LogicType {} Error", log_tag_, logic.type);
            break;
    }
    return false;
}

bool LogicCalcEngine::GetLogicResult(const AiDetectRstEl& target, LogicCalc& logic, bool bDebug) const {
    bool ret = false;

    if ((LogicType::Include == logic.type) || (LogicType::NonInclude == logic.type)) {
        ret = CalcIncludeOperation(logic);
    } else if (logic.list.empty()) {
        ret = CalcArithmeticOperation(target, logic);
    } else {
        ret = CalcLogicOperation(target, logic);
    }
    if (bDebug) {
        LOG_INFO("{} listSize:{} Get Ret:{}", LogicString(logic.type), logic.list.size(), ret);
    }
    return ret;
}

}  // namespace cosmo
