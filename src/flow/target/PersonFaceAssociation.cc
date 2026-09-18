#include "flow/target/PersonFaceAssociation.h"

#include <algorithm>
#include <cmath>

namespace cosmo {
namespace {

    bool IsCandidate(const AiDetectRstEl& person, const AiDetectRstEl& face) {
        const auto& body = person.box;
        const auto& box  = face.box;
        if (person.trackId < 0 || person.trackIdInfo.empty() || person.bFilter ||
            person.trackStatus == AITrackingStatus::LOSS || body.width <= 0 || body.height <= 0 ||
            box.width <= 0 || box.height <= 0 || face.confidence.label != "face" || face.bFilter ||
            !std::isfinite(face.confidence.confidence)) {
            return false;
        }
        const double center_x = box.x + box.width * 0.5;
        const double center_y = box.y + box.height * 0.5;
        if (center_x < body.x || center_x > body.x + body.width || center_y < body.y ||
            center_y > body.y + body.height * 0.5) {
            return false;
        }
        const int width =
            std::max(0, std::min(body.x + body.width, box.x + box.width) - std::max(body.x, box.x));
        const int height =
            std::max(0, std::min(body.y + body.height, box.y + box.height) - std::max(body.y, box.y));
        return static_cast<double>(width) * height >= 0.9 * box.width * box.height;
    }

}  // namespace

void AssociatePersonFaces(std::vector<AiDetectRstEl>& people, const std::vector<AiDetectRstEl>& faces,
                          int min_face_size) {
    std::vector<std::vector<size_t>> candidates(people.size());
    std::vector<size_t> owners(faces.size(), 0);
    for (size_t p = 0; p < people.size(); ++p) {
        auto& person            = people[p];
        person.relatedEl        = {};
        person.feature          = {};
        person.landmark         = {};
        person.matchInfo        = {};
        person.face_observation = {FaceObservationStatus::kNoFace, -1.0f};
        for (size_t f = 0; f < faces.size(); ++f) {
            if (IsCandidate(person, faces[f])) {
                candidates[p].push_back(f);
                ++owners[f];
            }
        }
    }
    for (size_t p = 0; p < people.size(); ++p) {
        auto& person = people[p];
        if (candidates[p].size() == 1 && owners[candidates[p].front()] == 1) {
            const auto& face               = faces[candidates[p].front()];
            person.relatedEl.bActive       = true;
            person.relatedEl.box           = face.box;
            person.relatedEl.confidence    = face.confidence;
            person.face_observation.status = FaceObservationStatus::kNotObserved;
            if (face.box.width < min_face_size || face.box.height < min_face_size) {
                person.bFilter                 = true;
                person.filterType              = AIFilterType::TargetFilter;
                person.filterDesc              = "Face below minimum size";
                person.face_observation.status = FaceObservationStatus::kLowQuality;
            }
        } else if (!person.bFilter) {
            person.bFilter    = true;
            person.filterType = AIFilterType::NoRelatedTarget;
            person.filterDesc = "No unambiguous associated face";
        }
    }
}

}  // namespace cosmo
