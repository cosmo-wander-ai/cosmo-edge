// FaceManager_Compare — Face Manager_ Compare implementation.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

#include "flow/face/FaceManager.h"
#include "service/detail/ServiceRegistry.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace fs = std::filesystem;

namespace cosmo {

bool FaceManager::FaceCompare(std::vector<std::string> sets, const AiFeature& feature,
                              AiDetectMatchHighScoreInfo& info, float param_limit_score) {
    info                 = {};
    info.setPicCount     = 0;
    const auto libraries = GetFaceLibs(std::move(sets));
    std::set<std::string> visited;
    std::vector<AiDetectMatchHighScoreInfo> dev_res{};
    for (const auto& face_lib : libraries) {
        if (!visited.insert(face_lib->GetId()).second)
            continue;
        size_t compared_count = 0;
        auto res              = face_lib->SearchFeature(feature, &compared_count);
        if (!res.first || !std::isfinite(res.second) || res.second < 0) {
            continue;
        }

        auto person = res.first->GetPerson();
        if (!person)
            continue;
        info.setPicCount += static_cast<int64_t>(compared_count);
        // Always record the best score even if below threshold
        FacePicPtr face_pic = res.first;
        AiDetectMatchHighScoreInfo temp{};
        temp.match_id       = face_pic->GetId();
        temp.match_degree   = res.second;
        temp.group_name     = face_lib->GetName();
        temp.group_id       = face_lib->GetId();
        temp.name           = person->GetName();
        temp.person_code    = person->GetSerialNumber();
        temp.person_id      = person->GetId();
        temp.base_image_url = cosmo::path::GetWebDir(
            (fs::path(cosmo::path::GetFaceLibPhotoDir()) / face_pic->GetId()).concat(".jpg"));
        dev_res.push_back(std::move(temp));
    }

    auto max_it =
        std::max_element(dev_res.begin(), dev_res.end(),
                         [](const AiDetectMatchHighScoreInfo& a, const AiDetectMatchHighScoreInfo& b) {
                             return a.match_degree < b.match_degree;
                         });
    if (max_it != dev_res.end()) {
        float limit_threshold = 0.0f;
        for (const auto& lib : libraries) {
            if (lib->GetId() == max_it->group_id) {
                limit_threshold = static_cast<float>(lib->GetThreshold());
                break;
            }
        }
        if (param_limit_score > 0) {
            limit_threshold = param_limit_score;
        }
        info.match_id       = max_it->match_id;
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
