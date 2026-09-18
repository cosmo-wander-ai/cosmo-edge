#pragma once

#include "infer/AiCommon.h"

namespace cosmo {

// A successful assessment means inference may run; it is not a negative match.
FaceObservation AssessFaceObservation(const AiDetectRstEl& target, bool have_area, float min_quality,
                                      bool front_only);
FaceObservationStatus FaceComparisonStatus(const AiDetectMatchHighScoreInfo& result);

}  // namespace cosmo
