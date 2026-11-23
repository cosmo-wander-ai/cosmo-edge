#pragma once
// Materialize results for the existing TaskDataLandmark/recognizer data path.
// This does not choose identity thresholds or perform image operations.
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "flow/classify/MaskBatchStage.h"
#include "flow/classify/MaskResultCommit.h"
#include "nn/device/axera/facelandmark/FaceTaskExecutor.h"
namespace cosmo::face_feature_task {
using helmet_candidate::FaceChainResult;
using mask_candidate::Roi;
struct Selection {
    std::vector<std::size_t> indices;
    std::vector<Roi> rois;
};
inline std::optional<Selection> Select(const std::vector<AiDetectRstEl>& targets, bool has_area,
                                       std::uint64_t frame_index) {
    Selection chosen;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& t = targets[i];
        if (t.classId != 1 ||
            mask_candidate::ClassifierEligibility(t, has_area) == mask_candidate::Eligibility::Skip)
            continue;
        if (t.frameIndex != frame_index || t.targetId.empty() ||
            t.confidence.source != AiAnnotationSource::Detection || t.confidence.atomic_code != "1000001" ||
            t.confidence.label != "face")
            return {};
        auto roi = mask_candidate::BoxToRoi(t.relatedEl.bActive ? t.relatedEl.box : t.box, 1920, 1080);
        if (!roi || chosen.rois.size() >= 64)
            return {};
        for (auto n : chosen.indices)
            if (targets[n].targetId == t.targetId)
                return {};
        chosen.indices.push_back(i);
        chosen.rois.push_back(*roi);
    }
    return chosen;
}
inline std::optional<std::vector<AiDetectRstEl>> Materialize(const std::vector<AiDetectRstEl>& targets,
                                                             const Selection& selected,
                                                             const FaceChainResult& result) {
    if (result.result != helmet_candidate::GuardResult::Complete ||
        result.landmarks.size() != selected.indices.size() ||
        result.features.size() != selected.indices.size() || selected.rois.size() != selected.indices.size())
        return {};
    auto staged = targets;
    for (std::size_t i = 0; i < selected.indices.size(); ++i) {
        const auto index = selected.indices[i];
        if (index >= staged.size())
            return {};
        auto& target = staged[index];
        auto actual  = mask_candidate::BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box,
                                               1920, 1080);
        if (!actual || *actual != selected.rois[i])
            return {};
        auto points = face_landmark_candidate::Project(*actual, result.landmarks[i]);
        if (!points)
            return {};
        target.landmark.landmark.clear();
        target.landmark.landmark.reserve(5);
        for (const auto& p : *points) {
            if (p.x < std::numeric_limits<int>::min() || p.x > std::numeric_limits<int>::max() ||
                p.y < std::numeric_limits<int>::min() || p.y > std::numeric_limits<int>::max())
                return {};
            target.landmark.landmark.emplace_back(static_cast<int>(p.x), static_cast<int>(p.y));
        }
        target.feature.feature.assign(result.features[i].begin(), result.features[i].end());
    }
    return staged;
}
}  // namespace cosmo::face_feature_task
