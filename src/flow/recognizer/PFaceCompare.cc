#include "flow/recognizer/PFaceCompare.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "service/face/dto/FaceDto.h"

namespace cosmo {
util::ErrorEnum ComparePictureFaces(std::vector<AiDetectRstEl>& targets,
                                    const std::vector<std::string>& libraries, float threshold,
                                    service::IFaceLibRepo& repo, service::IFaceFeature& matcher) {
    if (libraries.empty() || !std::isfinite(threshold) || threshold < 0 || threshold > 100) {
        return util::ErrorEnum::InvalidParam;
    }
    int64_t picture_count = 0;
    for (const auto& id : std::set<std::string>(libraries.begin(), libraries.end())) {
        auto lib = repo.GetFaceLib(id);
        if (!lib)
            return util::ErrorEnum::NoSuchId;
        picture_count += static_cast<int64_t>(service::FaceLibView::From(lib).faceCount);
    }
    if (picture_count == 0)
        return util::ErrorEnum::DependLibEmpty;

    for (auto& target : targets) {
        target.matchInfo = {};
        if (target.bFilter)
            continue;
        const auto& feature = target.feature.feature;
        if (feature.empty() ||
            !std::all_of(feature.begin(), feature.end(), [](float v) { return std::isfinite(v); }) ||
            std::none_of(feature.begin(), feature.end(), [](float v) { return v != 0; })) {
            return util::ErrorEnum::GetFeatureFailed;
        }
        auto& match       = target.matchInfo;
        match.matched     = matcher.FaceCompare(libraries, target.feature, match, threshold);
        match.setPicCount = picture_count;
        if (!std::isfinite(match.match_degree) || match.match_degree < 0) {
            return util::ErrorEnum::GetFeatureFailed;
        }
    }
    return util::ErrorEnum::Success;
}
}  // namespace cosmo
