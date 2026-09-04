// FaceManager_Compare — Face Manager_ Compare implementation.

#include <algorithm>
#include <filesystem>

#include "flow/face/FaceManager.h"
#include "service/detail/ServiceRegistry.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace fs = std::filesystem;

namespace cosmo {

bool FaceManager::FaceCompare(std::vector<std::string> sets, const AiFeature& feature,
                              AiDetectMatchHighScoreInfo& info, float param_limit_score) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    struct Candidate {
        AiDetectMatchHighScoreInfo info;
        float threshold{0.0f};
    };

    std::vector<Candidate> candidates;
    info.setPicCount = 0;
    for (const auto& set : sets) {
        for (const auto& face_lib : face_libs_) {
            if (set != face_lib->GetId()) {
                continue;
            }
            info.setPicCount += static_cast<int64_t>(face_lib->GetFaceCount());
            auto res = face_lib->SearchFeature(feature);
            if (!res.first) {
                continue;
            }

            // Always record the best score even if below threshold
            FacePicPtr face_pic = res.first;
            Candidate candidate;
            candidate.info.match_degree   = res.second;
            candidate.info.group_name     = face_lib->GetName();
            candidate.info.group_id       = face_lib->GetId();
            candidate.info.name           = face_pic->GetPerson()->GetName();
            candidate.info.person_code    = face_pic->GetPerson()->GetSerialNumber();
            candidate.info.person_id      = face_pic->GetPerson()->GetId();
            candidate.info.base_image_url = cosmo::path::GetWebDir(
                (fs::path(cosmo::path::GetFaceLibPhotoDir()) / face_pic->GetId()).concat(".jpg"));
            candidate.threshold =
                param_limit_score > 0 ? param_limit_score : static_cast<float>(face_lib->GetThreshold());
            candidate.info.matched = candidate.info.match_degree > candidate.threshold;
            candidates.push_back(std::move(candidate));
        }
    }

    if (candidates.empty()) {
        return false;
    }

    // A person is known if any selected library accepts its own best candidate.
    // This avoids a false stranger result when selected libraries use different thresholds.
    auto selected =
        std::max_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.info.matched != b.info.matched) {
                return !a.info.matched && b.info.matched;
            }
            return a.info.match_degree < b.info.match_degree;
        });
    const auto set_pic_count = info.setPicCount;
    info                     = selected->info;
    info.setPicCount         = set_pic_count;
    LOG_INFO("pace compare is {} {} {} {} {}", info.match_degree, info.name, info.group_name,
             info.base_image_url, info.person_code);
    return info.matched;
}

}  // namespace cosmo
