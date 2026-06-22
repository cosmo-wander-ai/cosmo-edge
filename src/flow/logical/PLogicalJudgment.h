// Image algorithm logical judgment

#pragma once

#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/PActionBase.h"
#include "flow/logical/LogicCalcEngine.h"

namespace cosmo {
struct PALogicalJudgmentParam {
    std::vector<AiConfidence> params;
    std::vector<MsgDynamicKeyValue> customs;
};

class PLogicalJudgment : public PActionBase {
public:
    PLogicalJudgment(const std::string& taskId, ActionNode& action);
    ~PLogicalJudgment();

    bool ActionInit() override;
    util::ErrorEnum HandPic(AlgDataPtr algData) override;

    // Modify parameters based on existing ones
    bool ModifyParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;
    // Set parameters - clear previous ones and set fully new ones
    bool SetParam(const std::string& taskId, std::vector<MsgDynamicKeyValue>& params) override;

    //	void ActionInfo(std::vector<ActionRuntimeInfo> &actionInfo) override;
private:
    [[nodiscard]] bool ValidKey(MsgDynamicKeyValue& param) const;
    bool AnalysisKey(MsgDynamicKeyValue& param);
    bool AnalysisCustomKey(MsgDynamicKeyValue& param);

private:
    std::shared_mutex mtx_;
    size_t handle_frames_{0};
    size_t filter_frames_{0};
    LogicCalc logic_;
    PALogicalJudgmentParam params_;
    LogicCalcEngine calc_engine_;
    util::ErrorEnum action_status_{util::ErrorEnum::Success};
};
using PLogicalJudgmentPtr = std::shared_ptr<PLogicalJudgment>;
}  // namespace cosmo
