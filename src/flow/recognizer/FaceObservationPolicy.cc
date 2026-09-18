#include "flow/recognizer/FaceObservationPolicy.h"

#include <cmath>

namespace cosmo {

FaceObservation AssessFaceObservation(const AiDetectRstEl& target, bool have_area, float min_quality,
                                      bool front_only) {
    if (!target.relatedEl.bActive) {
        return {FaceObservationStatus::kNoFace, -1.0f};
    }
    if (target.bFilter || !target.areaSign.shielded_areas.empty() ||
        (have_area && target.areaSign.areas.empty())) {
        return {FaceObservationStatus::kFiltered, -1.0f};
    }
    AiConfidence quality;
    AiConfidence angle;
    if (!GetFaceQuality(target.relatedEl.classifyRst, quality) || !std::isfinite(quality.confidence) ||
        quality.confidence < min_quality ||
        (front_only && (!GetFaceAngle(target.relatedEl.classifyRst, angle) ||
                        !std::isfinite(angle.confidence) || angle.label != "frontFace"))) {
        return {FaceObservationStatus::kLowQuality, -1.0f};
    }
    if (target.relatedEl.landmark.landmark.size() < 5) {
        return {FaceObservationStatus::kUnavailable, quality.confidence};
    }
    return {FaceObservationStatus::kNotObserved, quality.confidence};
}

FaceObservationStatus FaceComparisonStatus(const AiDetectMatchHighScoreInfo& result) {
    if (!std::isfinite(result.match_degree) || result.match_degree < 0.0f || result.person_id.empty()) {
        return FaceObservationStatus::kUnavailable;
    }
    if (result.matched) {
        return FaceObservationStatus::kMatched;
    }
    // A negative result is usable only when every selected library was searchable.
    return result.setPicCount > 0 ? FaceObservationStatus::kUnmatched : FaceObservationStatus::kUnavailable;
}

}  // namespace cosmo
