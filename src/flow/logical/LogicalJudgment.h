#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/logical/LogicCalcEngine.h"
#include "flow/overview/OverviewRecordAiRst.h"
#include "util/MsgDynamicElement.h"

namespace cosmo {

struct LogicalJudgmentLogicParam {
    std::string alg_code;
    std::string label;
    std::string category;
    float value{0.0f};
};

struct BALogicalJudgmentParam {
    std::vector<LogicalJudgmentLogicParam> params;
    std::vector<MsgDynamicKeyValue> customs;
};

class LogicalJudgment : public AlgActionBase {
public:
    LogicalJudgment(const std::string& taskId, ActionNode& actionLogicalJudgment);
    ~LogicalJudgment();

    // Modify parameters based on existing ones
    bool ModifyParam(const std::string& channelId, const std::string& taskId,
                     std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous ones and set fully new ones
    bool SetParam(const std::string& channelId, const std::string& taskId,
                  std::vector<MsgDynamicKeyValue>& params) override;

    // Get overlay info
    MsgOverviewMem GetOverviewInfo(const std::string& channelId, const std::string& taskId,
                                   int64_t streamIndex = -1, int64_t from = -1, int64_t to = -1) override;

    [[nodiscard]] bool LogicTest(AiDetectRstEl& target);

    void HandFrame(AlgDataPtr algData) override;

private:
    bool AnalysisCustomKey(MsgDynamicKeyValue& param, MsgDynamicKeyValue& localParamEl);
    bool AnalysisKey(MsgDynamicKeyValue& param, LogicalJudgmentLogicParam& localParamEl);

    LogicCalc logic_;
    BALogicalJudgmentParam params_;
    LogicCalcEngine calc_engine_;
    OverviewRecordAiRst overview_rec_inst_;
};
using LogicalJudgmentPtr = std::shared_ptr<LogicalJudgment>;
}  // namespace cosmo
