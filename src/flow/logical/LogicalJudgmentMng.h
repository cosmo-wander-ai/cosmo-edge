#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/logical/LogicalJudgment.h"

namespace cosmo {
class LogicalJudgmentMng : public VectorActionMng<LogicalJudgment> {
public:
    LogicalJudgmentMng() : VectorActionMng("LogicalJudgmentMng") {}

    bool RegistTaskQueue(AlgTaskUnit& param);
    bool RemoveTaskQueue(AlgTaskUnit& param);
    bool LogicTest(const std::string& taskId, const MsgTarget& msgTarget);

protected:
    bool MatchForDelete(const InstPtr& existing, const InstPtr& target) const override {
        return (existing->GetTaskId() == target->GetTaskId()) &&
               (existing->GetFlowActionId() == target->GetFlowActionId());
    }

private:
    [[nodiscard]] LogicalJudgmentPtr FindUniqueByTaskId(const std::string& taskId, const char* caller);
};
}  // namespace cosmo
