#pragma once

#include <string>

#include "flow/action/ActionInstMngBase.h"
#include "flow/alarm/AreaAlarm.h"

namespace cosmo {
class AreaAlarmMng : public VectorActionMng<AreaAlarm> {
public:
    AreaAlarmMng() : VectorActionMng("AreaAlarmMng") {}

protected:
    bool MatchForDelete(const InstPtr& existing, const InstPtr& target) const override {
        return (existing->GetTaskId() == target->GetTaskId()) &&
               (existing->GetFlowActionId() == target->GetFlowActionId());
    }
};
}  // namespace cosmo
