#include "flow/target/PersonFaceAssociation.h"

#include "flow/target/TargetAssociation.h"

namespace cosmo {

void UpdateAssociatedFaceObservations(std::vector<AiDetectRstEl>& people) {
    for (auto& person : people) {
        person.feature          = {};
        person.landmark         = {};
        person.matchInfo        = {};
        person.face_observation = {FaceObservationStatus::kNoFace, -1.0f};
        if (person.association_status == TargetAssociationStatus::kMatched) {
            person.face_observation.status = FaceObservationStatus::kNotObserved;
        } else if (person.association_status == TargetAssociationStatus::kTooSmall) {
            person.face_observation.status = FaceObservationStatus::kLowQuality;
        } else if (person.association_status == TargetAssociationStatus::kUnavailable) {
            person.face_observation.status = FaceObservationStatus::kUnavailable;
        }
    }
}

void AssociatePersonFaces(std::vector<AiDetectRstEl>& people, const std::vector<AiDetectRstEl>& faces,
                          int min_face_size) {
    TargetAssociationConfig config;
    config.min_size = min_face_size;
    // This legacy helper receives detections already filtered by the detector.
    config.confidence = 0.0f;
    AssociateTargets(people, faces, config);
    UpdateAssociatedFaceObservations(people);
}

}  // namespace cosmo
