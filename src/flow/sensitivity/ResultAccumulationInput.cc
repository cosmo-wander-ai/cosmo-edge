#include "flow/sensitivity/ResultAccumulationInput.h"

#include <cmath>

namespace cosmo {

AccumulationObservation AssessAccumulationObservation(const AiDetectRstEl& target, bool complete,
                                                      bool recognition) {
    if (!complete || (recognition && target.face_observation.status == FaceObservationStatus::kUnavailable)) {
        return {TrackObservation::kUnavailable};
    }
    // Keep the existing stranger rule: any successful match saves this track.
    if (recognition && target.face_observation.status == FaceObservationStatus::kMatched) {
        return {TrackObservation::kPositive};
    }
    if (target.bFilter || !target.areaSign.shielded_areas.empty() || target.areaSign.areas.empty()) {
        return {};
    }
    if (recognition) {
        if (target.face_observation.status == FaceObservationStatus::kUnmatched && target.relatedEl.bActive &&
            std::isfinite(target.face_observation.quality)) {
            return {TrackObservation::kNegative, target.face_observation.quality};
        }
        return {};
    }
    if (!target.logic_observation.has_value()) {
        return {};
    }
    if (*target.logic_observation) {
        return {TrackObservation::kPositive};
    }
    // Prefer the largest usable parent snapshot, independently of model score scales.
    const auto& box     = target.box;
    const float quality = static_cast<float>(box.width) * static_cast<float>(box.height);
    if (box.width <= 0 || box.height <= 0 || !std::isfinite(quality)) {
        return {};
    }
    return {TrackObservation::kNegative, quality};
}

}  // namespace cosmo
