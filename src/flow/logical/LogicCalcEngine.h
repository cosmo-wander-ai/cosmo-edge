// Common logic calculation engine — shared by streaming and image

#pragma once

#include <functional>
#include <string>

#include "infer/AiCommon.h"
#include "util/LogicCalc.h"

namespace cosmo {

// Parameter callback — makes Engine independent of specific parameter storage structures
using ParamLimitValueFn = std::function<bool(const std::string& label, float& value)>;
using ValueIsIncludeFn  = std::function<bool(const std::string& key, const std::string& value)>;

class LogicCalcEngine {
public:
    LogicCalcEngine(const std::string& logTag, ParamLimitValueFn paramFn, ValueIsIncludeFn includeFn);

    [[nodiscard]] bool GetLogicResult(const AiDetectRstEl& target, LogicCalc& logic,
                                      bool bDebug = false) const;

    // Pure function — independent of parameters
    [[nodiscard]] static bool GetAiOutValue(const AiDetectRstEl& target, const std::string& label,
                                            float& value);
    [[nodiscard]] static bool GetAiOutValueAttr(const AiDetectRstEl& target, const std::string& label,
                                                std::string& value);

private:
    [[nodiscard]] bool GetLeftRightValue(const AiDetectRstEl& target, LogicCalc& logic, float& lValue,
                                         float& rValue) const;
    [[nodiscard]] bool GetLeftRightValueAttr(const AiDetectRstEl& target, LogicCalc& logic,
                                             std::string& lValue, std::string& rValue) const;
    [[nodiscard]] bool CalcArithmeticOperation(const AiDetectRstEl& target, LogicCalc& logic) const;
    [[nodiscard]] bool CalcArithmeticOperationAttr(const AiDetectRstEl& target, LogicCalc& logic) const;
    [[nodiscard]] bool CalcLogicOperation(const AiDetectRstEl& target, LogicCalc& logic) const;
    [[nodiscard]] bool CalcIncludeOperation(LogicCalc& logic) const;

    std::string log_tag_;
    ParamLimitValueFn param_fn_;
    ValueIsIncludeFn include_fn_;
};

}  // namespace cosmo
