#pragma once

#include "flow/common/AlgAlarmTypes.h"
#include "service/ai/VisualDecisionFence.h"

namespace cosmo::alarm {
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
    auto runs = VisualRuns(source);
    destination.visualRuns.insert(destination.visualRuns.end(), runs.begin(), runs.end());
    service::NormalizeVisualRuns(destination.visualRuns);
}
}  // namespace cosmo::alarm
