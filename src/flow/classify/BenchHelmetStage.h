#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "flow/classify/HelmetResultCommit.h"

namespace cosmo::bench_ax {
inline bool HelmetScene(const std::string& scene, const std::string& detector,
                        const std::string& classifier) noexcept {
    return scene == "930007463" && detector == "9275710" && classifier == "7982161";
}
struct HelmetSelection {
    std::vector<std::size_t> indices;
    std::vector<helmet_candidate::Roi> rois;
};
inline std::optional<HelmetSelection> SelectHelmetTargets(
    const std::vector<helmet_candidate::BoundTarget>& targets, const helmet_candidate::FrameKey& key,
    bool has_area) {
    using namespace helmet_candidate;
    if (!ValidKey(key))
        return {};
    HelmetSelection result;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& bound  = targets[i];
        const auto& target = bound.target;
        if (!(bound.frame == key) || target.frameIndex != key.frame_index || target.targetId.empty())
            return {};
        for (std::size_t j = 0; j < i; ++j)
            if (targets[j].target.targetId == target.targetId)
                return {};
        if (target.classId != 1 || ClassifierEligibility(target, has_area) == Eligibility::Skip)
            continue;
        if (target.confidence.source != AiAnnotationSource::Detection ||
            target.confidence.atomic_code != "9275710" || target.confidence.label != "Pedestrian" ||
            !std::isfinite(target.confidence.confidence) || target.confidence.confidence < 0.f ||
            target.confidence.confidence > 1.f)
            return {};
        const auto roi = BoxToRoi(target.relatedEl.bActive ? target.relatedEl.box : target.box, 1920, 1080);
        if (!roi || result.indices.size() >= 64)
            return {};
        result.indices.push_back(i);
        result.rois.push_back(*roi);
    }
    return result;
}
template <class NextId>
std::optional<helmet_candidate::TaskOutput> MaterializeHelmet(
    const std::vector<helmet_candidate::BoundTarget>& input, const helmet_candidate::FrameKey& key,
    bool has_area, const HelmetSelection& selection, const std::vector<std::array<float, 2>>& scores,
    NextId&& next_id) {
    using namespace helmet_candidate;
    const auto checked = SelectHelmetTargets(input, key, has_area);
    if (!checked || checked->indices != selection.indices || checked->rois != selection.rois ||
        scores.size() != selection.indices.size())
        return {};
    TaskOutput result;
    result.reserve(input.size());
    for (const auto& x : input)
        result.push_back(x.target);
    std::vector<std::string> used;
    for (const auto& x : result) {
        for (const auto& c : x.classifyRst)
            if (!c.measurementId.empty())
                used.push_back(c.measurementId);
        for (const auto& c : x.relatedEl.classifyRst)
            if (!c.measurementId.empty())
                used.push_back(c.measurementId);
    }
    constexpr const char* labels[]{"helmet", "nohelmet"};
    for (std::size_t i = 0; i < scores.size(); ++i) {
        auto& target = result[selection.indices[i]];
        for (std::size_t j = 0; j < 2; ++j) {
            if (!std::isfinite(scores[i][j]) || scores[i][j] < 0.f || scores[i][j] > 1.f)
                return {};
            AiConfidence c;
            c.atomic_code   = "7982161";
            c.source        = AiAnnotationSource::Classification;
            c.measurementId = next_id();
            if (c.measurementId.empty() || std::find(used.begin(), used.end(), c.measurementId) != used.end())
                return {};
            used.push_back(c.measurementId);
            c.confidence = scores[i][j];
            c.label      = labels[j];
            target.classifyRst.push_back(c);
            if (target.relatedEl.bActive)
                target.relatedEl.classifyRst.push_back(c);
        }
    }
    return result;
}
}  // namespace cosmo::bench_ax
