#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "util/LogicCalc.h"
#include "util/MsgDynamicElement.h"

namespace cosmo {

struct BAActionBranchParam {
    std::vector<MsgDynamicKeyValue> customs;
};

class ActionBranch : public AlgActionBase {
public:
    ActionBranch(const std::string& taskId, ActionNode& actionActionBranch);
    ~ActionBranch();

    // Modify parameter - modify based on existing parameters
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameter - clear previous parameters and set new ones completely
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Get overlay information
    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    void HandFrame(AlgDataPtr algData) override;

private:
    bool AnalysisCustomKey(MsgDynamicKeyValue& param, MsgDynamicKeyValue& localParamEl);

    bool GetLogicResult(LogicCalc& logic, bool debug = false);
    bool ValueIsInclude(const std::string& key, const std::string& value);
    bool CalcIncludeOperation(LogicCalc& logic);
    bool CalcArithmeticOperation(LogicCalc& logic);
    bool CalcLogicOperation(LogicCalc& logic);
    std::string GetParamValue(const std::string& key);
    std::string GetLogicValue(const std::string& key, const std::vector<std::string>& keys);

    size_t distribute_frames_{0};
    LogicCalc logic_;
    BAActionBranchParam params_;
    OverviewRecordAiRst overview_rec_inst_;
};
using ActionBranchPtr = std::shared_ptr<ActionBranch>;
}  // namespace cosmo
