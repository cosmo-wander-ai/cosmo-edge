#pragma once

#include "flow/common/AlgAlarmTypes.h"
#include "service/ai/VisualDecisionFence.h"

namespace cosmo::alarm {
// Decisions retain an event if any independent candidate survives. Ordinary
// target batches also remove rejected targets and their overlays; associated
// composite alarms retain their original geometry when there is no 1:1 map.
bool RetainVisualTargets(DataAlarmUnit& unit, const std::vector<bool>& keep);

inline service::VisualDecisionRuns VisualRuns(const DataAlarmUnit& unit) {
    auto runs = unit.visualRuns;
    if (unit.visualRun)
        runs.push_back(unit.visualRun);
    service::NormalizeVisualRuns(runs);
    return runs;
}

// Associated flows can carry independent configurations even when one legacy
// scalar target becomes the combined alarm's representative. Retain every audit
// and every fence so edits/stops of any contributor invalidate the whole event.
inline void AppendVisualState(DataAlarmUnit& destination, const DataAlarmUnit& source) {
    destination.visualJudgments.insert(destination.visualJudgments.end(), source.visualJudgments.begin(),
                                       source.visualJudgments.end());
    destination.visualCandidates.insert(destination.visualCandidates.end(), source.visualCandidates.begin(),
                                        source.visualCandidates.end());
    destination.visualAuditLeases.insert(destination.visualAuditLeases.end(),
                                         source.visualAuditLeases.begin(), source.visualAuditLeases.end());
    auto runs = VisualRuns(source);
    destination.visualRuns.insert(destination.visualRuns.end(), runs.begin(), runs.end());
    service::NormalizeVisualRuns(destination.visualRuns);
}
}  // namespace cosmo::alarm
