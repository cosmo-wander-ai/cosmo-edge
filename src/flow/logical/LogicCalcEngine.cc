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

namespace {
    const AiConfidence* FindAiOutResult(const AiDetectRstEl& target, const std::string& label) {
        if (label == target.confidence.label)
            return &target.confidence;
        for (const auto& result : target.classifyRst) {
            if (result.label == label)
                return &result;
        }
        if (target.relatedEl.bActive) {
            for (const auto& result : target.relatedEl.classifyRst) {
                if (result.label == label)
                    return &result;
            }
        }
        return nullptr;
    }

    void AppendAnnotation(std::vector<AiTargetAnnotation>& destination, const AiTargetAnnotation& value) {
        const auto duplicate =
            std::find_if(destination.begin(), destination.end(), [&](const auto& existing) {
                return existing.label == value.label && existing.atomic_code == value.atomic_code &&
                       existing.source == value.source && existing.score == value.score;
            });
        if (duplicate == destination.end())
            destination.push_back(value);
    }
}  // namespace

bool LogicCalcEngine::GetAiOutValue(const AiDetectRstEl& target, const std::string& label, float& value) {
    if (const auto* result = FindAiOutResult(target, label)) {
        value = result->confidence;
        return true;
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

bool LogicCalcEngine::CalcArithmeticOperationAttr(const AiDetectRstEl& target, LogicCalc& logic,
                                                  std::vector<AiConfidence>* evidence) const {
    std::string lValue;
    std::string rValue;
    if (!GetLeftRightValueAttr(target, logic, lValue, rValue))
        return false;

    switch (logic.type) {
        case LogicType::Equal: {
            if (lValue == rValue && evidence) {
                const auto& category =
                    logic.keyLElements[0] == key::AI_PARAM ? logic.keyRElements[2] : logic.keyLElements[2];
                const auto found = std::find_if(target.attrRst.begin(), target.attrRst.end(),
                                                [&](const auto& attr) { return attr.category == category; });
                if (found != target.attrRst.end()) {
                    AiConfidence measured{found->label, found->atomic_code, found->confidence, found->source,
                                          found->measurementId};
                    if (MeasuredAnnotation(measured))
                        evidence->push_back(std::move(measured));
                }
            }
            return lValue == rValue;
        }
        case LogicType::NEQ:
            return lValue != rValue;
        default:
            LOG_WARN("[{}] LogicType {} Error", log_tag_, logic.type);
            break;
    }
    return false;
}

bool LogicCalcEngine::CalcArithmeticOperation(const AiDetectRstEl& target, LogicCalc& logic,
                                              std::vector<AiConfidence>* evidence) const {
    float lValue = 0.0;
    float rValue = 0.0;
    auto ret     = GetLeftRightValue(target, logic, lValue, rValue);
    if (!ret) {
        return CalcArithmeticOperationAttr(target, logic, evidence);
    }
    bool matched = false;
    switch (logic.type) {
        case LogicType::Equal:
            matched = lValue == rValue;
            break;
        case LogicType::NEQ:
            matched = lValue != rValue;
            break;
        case LogicType::Greater:
            matched = lValue > rValue;
            break;
        case LogicType::GE:
            matched = lValue >= rValue;
            break;
        case LogicType::Less:
            matched = lValue < rValue;
            break;
        case LogicType::LE:
            matched = lValue <= rValue;
            break;
        default:
            LOG_WARN("[{}] LogicType {} Error", log_tag_, logic.type);
            break;
    }
    const bool aiOnRight = logic.keyLElements[0] == key::AI_PARAM;
    // A low/absent class is not evidence for an invented opposite class or 1-score.
    const bool positive = aiOnRight ? (logic.type == LogicType::Less || logic.type == LogicType::LE)
                                    : (logic.type == LogicType::Greater || logic.type == LogicType::GE);
    if (matched && evidence && positive) {
        const auto& label = aiOnRight ? logic.keyRElements[1] : logic.keyLElements[1];
        if (const auto* result = FindAiOutResult(target, label)) {
            if (MeasuredAnnotation(*result))
                evidence->push_back(*result);
        }
    }
    return matched;
}

bool LogicCalcEngine::CalcLogicOperation(const AiDetectRstEl& target, LogicCalc& logic,
                                         std::vector<AiConfidence>* evidence) const {
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
                resInit = resInit || GetLogicResult(target, logicItem, false, evidence);
            } break;
            case LogicType::AND: {
                resInit = resInit && GetLogicResult(target, logicItem, false, evidence);
            } break;
            case LogicType::NOR: {
                return GetLogicResult(target, logicItem, false, evidence);
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

bool LogicCalcEngine::GetLogicResult(const AiDetectRstEl& target, LogicCalc& logic, bool bDebug,
                                     std::vector<AiConfidence>* evidence) const {
    const auto evidenceStart = evidence ? evidence->size() : 0;
    bool ret                 = false;

    if ((LogicType::Include == logic.type) || (LogicType::NonInclude == logic.type)) {
        ret = CalcIncludeOperation(logic);
    } else if (logic.list.empty()) {
        ret = CalcArithmeticOperation(target, logic, evidence);
    } else {
        ret = CalcLogicOperation(target, logic, evidence);
    }
    if (!ret && evidence)
        evidence->resize(evidenceStart);
    if (bDebug) {
        LOG_INFO("{} listSize:{} Get Ret:{}", LogicString(logic.type), logic.list.size(), ret);
    }
    return ret;
}

bool LogicCalcEngine::ApplyLogicResult(AiDetectRstEl& target, LogicCalc& logic) const {
    target.alarmAnnotations.clear();
    target.relatedEl.alarmAnnotations.clear();
    for (auto& related : target.relatedEls)
        related.alarmAnnotations.clear();
    for (auto& sub : target.subs)
        sub.alarmAnnotations.clear();
    std::vector<AiConfidence> evidence;
    const bool result = GetLogicResult(target, logic, false, &evidence);
    for (const auto& measured : evidence) {
        auto annotation = MeasuredAnnotation(measured);
        if (!annotation)
            continue;
        bool relatedMatch  = false;
        auto attachRelated = [&](AiDetectRelatedEl& related) {
            if (measured.measurementId.empty())
                return;
            const auto found =
                std::find_if(related.classifyRst.begin(), related.classifyRst.end(),
                             [&](const auto& item) { return item.measurementId == measured.measurementId; });
            if (found != related.classifyRst.end()) {
                AppendAnnotation(related.alarmAnnotations, *annotation);
                relatedMatch = true;
            }
        };
        if (target.relatedEl.bActive)
            attachRelated(target.relatedEl);
        for (auto& related : target.relatedEls)
            attachRelated(related);
        for (auto& sub : target.subs)
            attachRelated(sub);
        if (!relatedMatch)
            AppendAnnotation(target.alarmAnnotations, *annotation);
    }
    return result;
}

}  // namespace cosmo
