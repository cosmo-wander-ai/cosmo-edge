#pragma once

#include "flow/sensitivity/PositiveTrackEvidence.h"
#include "infer/AiCommon.h"

namespace cosmo {

struct AccumulationObservation {
    TrackObservation status{TrackObservation::kSkipped};
    float snapshot_quality{-1.0f};
};

// Adapts validated upstream results without interpreting a missing result as false.
AccumulationObservation AssessAccumulationObservation(const AiDetectRstEl& target, bool complete,
                                                      bool recognition);

}  // namespace cosmo
