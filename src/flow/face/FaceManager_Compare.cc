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
    std::vector<AiDetectMatchHighScoreInfo> dev_res{};
    for (auto set : sets) {
        for (auto face_lib : face_libs_) {
            if (set != face_lib->GetId()) {
                LOG_INFO("face origine lib {} not eql compare lib {}", face_lib->GetId(), set);
                continue;
            }
            auto res = face_lib->SearchFeature(feature);
            if (!res.first) {
                continue;
            }

            // Always record the best score even if below threshold
            FacePicPtr face_pic = res.first;
            AiDetectMatchHighScoreInfo temp{};
            temp.match_degree   = res.second;
            temp.group_name     = face_lib->GetName();
            temp.group_id       = face_lib->GetId();
            temp.name           = face_pic->GetPerson()->GetName();
            temp.person_code    = face_pic->GetPerson()->GetSerialNumber();
            temp.person_id      = face_pic->GetPerson()->GetId();
            temp.base_image_url = cosmo::path::GetWebDir(
                (fs::path(cosmo::path::GetFaceLibPhotoDir()) / face_pic->GetId()).concat(".jpg"));
            dev_res.push_back(std::move(temp));
        }
    }

    auto max_it =
        std::max_element(dev_res.begin(), dev_res.end(),
                         [](const AiDetectMatchHighScoreInfo& a, const AiDetectMatchHighScoreInfo& b) {
                             return a.match_degree < b.match_degree;
                         });
    if (max_it != dev_res.end()) {
        float limit_threshold = 0.0f;
        if (auto lib = GetFaceLib(max_it->group_id)) {
            limit_threshold = static_cast<float>(lib->GetThreshold());
        }
        if (param_limit_score > 0) {
            limit_threshold = param_limit_score;
        }
        info.match_degree   = max_it->match_degree;
        info.name           = max_it->name;
        info.group_id       = max_it->group_id;
        info.base_image_url = max_it->base_image_url;
        info.group_name     = max_it->group_name;
        info.person_id      = max_it->person_id;
        info.person_code    = max_it->person_code;
        info.matched        = (info.match_degree > limit_threshold);
    } else {
        dev_res.clear();
        return false;
    }
    LOG_INFO("pace compare is {} {} {} {} {}", info.match_degree, info.name, info.group_name,
             info.base_image_url, info.person_code);
    dev_res.clear();
    return info.matched;
}

}  // namespace cosmo
