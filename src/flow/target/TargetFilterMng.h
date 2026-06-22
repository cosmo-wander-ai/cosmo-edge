// Instance manager for TargetFilter actions.

#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/target/TargetFilter.h"

namespace cosmo {
class TargetFilterMng : public VectorActionMng<TargetFilter> {
public:
    TargetFilterMng() : VectorActionMng("TargetFilterMng") {}

protected:
    bool MatchForDelete(const InstPtr& existing, const InstPtr& target) const override {
        return (existing->GetTaskId() == target->GetTaskId()) &&
               (existing->GetFlowActionId() == target->GetFlowActionId());
    }
};
}  // namespace cosmo
