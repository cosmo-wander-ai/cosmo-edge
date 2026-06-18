#pragma once

#include <string>

#include "flow/action/ActionBranch.h"
#include "flow/action/ActionInstMngBase.h"

namespace cosmo {
class ActionBranchMng : public VectorActionMng<ActionBranch> {
public:
    ActionBranchMng() : VectorActionMng("ActionBranchMng") {}

protected:
    bool MatchForDelete(const InstPtr& existing, const InstPtr& target) const override {
        return (existing->GetTaskId() == target->GetTaskId()) &&
               (existing->GetFlowActionId() == target->GetFlowActionId());
    }
};
}  // namespace cosmo
