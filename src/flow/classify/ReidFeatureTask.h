#pragma once
// Candidate real-time handoff, restricted to actual person detections from the same native frame.
#include <cmath>
#include <optional>

#include "flow/classify/GarmentBatchStage.h"
#include "nn/device/axera/reid/ReidPairSdkExecutor.h"
namespace cosmo::reid_feature_task {
using garment_candidate::Roi;
struct Selection {
    std::vector<std::size_t> indices;
    std::vector<Roi> rois;
};
inline std::optional<Selection> Select(const std::vector<AiDetectRstEl>& targets, bool has_area,
                                       std::uint64_t frame_index) {
    Selection selected;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& t = targets[i];
        if (t.classId != 1)
            continue;
        const auto decision = garment_candidate::ClassifierEligibility(t, has_area);
        if (decision == garment_candidate::Eligibility::Skip)
            continue;
        if (decision != garment_candidate::Eligibility::Include ||
            t.confidence.source != AiAnnotationSource::Detection || t.frameIndex != frame_index ||
            t.targetId.empty() || t.confidence.atomic_code != "1001003" ||
            t.confidence.label != "pedestrian" || !std::isfinite(t.confidence.confidence) ||
            t.confidence.confidence < 0.f || t.confidence.confidence > 1.f)
            return {};
        const auto roi =
            garment_candidate::BoxToRoi(t.relatedEl.bActive ? t.relatedEl.box : t.box, 1920, 1080);
        if (!roi || selected.rois.size() >= 64)
            return {};
        for (auto index : selected.indices)
            if (targets[index].targetId == t.targetId)
                return {};
        selected.indices.push_back(i);
        selected.rois.push_back(*roi);
    }
    return selected;
}
inline std::optional<std::vector<AiDetectRstEl>> Materialize(const std::vector<AiDetectRstEl>& targets,
                                                             const Selection& selected,
                                                             const helmet_candidate::ReidPairResult& result) {
    if (result.result != helmet_candidate::GuardResult::Complete ||
        result.features.size() != selected.indices.size() || selected.rois.size() != selected.indices.size())
        return {};
    auto staged = targets;
    for (auto& t : staged)
        t.feature.feature.clear();
    for (std::size_t i = 0; i < selected.indices.size(); ++i) {
        const auto index = selected.indices[i];
        if (index >= staged.size())
            return {};
        auto& t     = staged[index];
        auto actual = garment_candidate::BoxToRoi(t.relatedEl.bActive ? t.relatedEl.box : t.box, 1920, 1080);
        if (!actual || *actual != selected.rois[i])
            return {};
        t.feature.feature.assign(result.features[i].begin(), result.features[i].end());
    }
    return staged;
}
}  // namespace cosmo::reid_feature_task
