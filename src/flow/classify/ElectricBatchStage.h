#pragma once
// Isolated candidate: stages task-local scores; it is not wired into a scene,
// classifier queue, native SDK runner, or event publisher.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "flow/classify/ElectricSceneContract.h"
#include "infer/AiCommon.h"

namespace cosmo::electric_candidate {
using Roi = std::array<std::int32_t, 4>;
struct FrameKey {
    std::string channel;
    std::uint64_t generation{0}, token{0}, frame_index{0};
    std::int64_t timestamp{0};
};
inline bool operator==(const FrameKey& a, const FrameKey& b) noexcept {
    return a.channel == b.channel && a.generation == b.generation && a.token == b.token &&
           a.frame_index == b.frame_index && a.timestamp == b.timestamp;
}
inline bool ValidKey(const FrameKey& k) noexcept {
    return !k.channel.empty() && k.generation && k.token;
}
using InferenceKey = electric_scene_candidate::InferenceKey;
using Admission    = electric_scene_candidate::Admission;
struct BoundTarget {
    AiDetectRstEl target;
    FrameKey frame;
    InferenceKey inference;
};
using Scene = electric_scene_candidate::Scene;
// The caller supplies the already-resolved threshold and must bind the
// configuration generation. IDs from reference config, scene template and
// tensor channel are separate; see ElectricSceneContract.h.
struct Thresholds {
    Scene scene;
    float value;
};
inline bool ValidThresholds(const Thresholds& t) noexcept {
    return electric_scene_candidate::Lookup(t.scene) && electric_scene_candidate::IsProbability(t.value);
}
inline std::optional<bool> Decide(const std::array<float, 3>& s, const Thresholds& t) noexcept {
    if (!ValidThresholds(t))
        return {};
    for (float x : s)
        if (!electric_scene_candidate::IsProbability(x))
            return {};
    const auto* spec = electric_scene_candidate::Lookup(t.scene);
    return s[spec->tensor_channel] >= t.value;
}
inline std::optional<Roi> BoxToRoi(const util::Box& box, int width, int height) noexcept {
    if (width != 1920 || height != 1080 || box.width <= 0 || box.height <= 0)
        return {};
    const std::int64_t x0 = box.x, y0 = box.y, x1 = x0 + static_cast<std::int64_t>(box.width),
                       y1 = y0 + static_cast<std::int64_t>(box.height);
    // This candidate admits frame-contained detector ROIs only; no silent clipping.
    if (x0 < 0 || y0 < 0 || x1 > width || y1 > height || x0 >= x1 || y0 >= y1 || y1 - y0 < 2)
        return {};
    return Roi{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0), static_cast<std::int32_t>(x1),
               static_cast<std::int32_t>(y1)};
}
enum class Eligibility { Include, Skip, Unsupported };
// Match the existing AiClassifier::HandFramesEx task filter exactly. This
// helper has no side effects; task filters and area signage must already have
// run on the current task-local tracker result before it is called.
inline Eligibility ClassifierEligibility(const AiDetectRstEl& t, bool input_has_area) noexcept {
    if (!t.bForceClassify &&
        (t.bFilter || (input_has_area && (!t.areaSign.shielded_areas.empty() || t.areaSign.areas.empty()))))
        return Eligibility::Skip;
    return Eligibility::Include;
}
enum class RunStatus { Complete, Cancelled, KnownFailure, CompletionUnknown };
enum class BatchStatus {
    Complete,
    Cancelled,
    InvalidConfig,
    InvalidTarget,
    UnsupportedGraph,
    CapacityExceeded,
    ExecutionFailed,
    CompletionUnknown
};
struct CompactResult {
    std::string target_id;
    FrameKey frame;
    InferenceKey inference;
    Roi roi{};
    std::array<float, 3> scores{};
    bool scene_unsafe{false};
    bool use_related_box{false};
};
struct BatchResult {
    BatchStatus status{BatchStatus::ExecutionFailed};
    std::vector<CompactResult> outputs;
    Scene scene{};
    float threshold{};
    InferenceKey inference;
};
// Caller must supply a task-local post-filter/tracker snapshot with a genuine
// decoder-issued frame key, not an arbitrary key copied from input fields.
// `eligible` must be pure, with no task history/event side effects. `current`
// must check task, model and configuration generations. `run` must retain the
// source/model/IO owners and poison global AX on unknown completion. This
// function returns staged data only: publication needs an atomic combined
// task-and-model commit gate at the existing graph's publication boundary.
template <class Current, class Eligible, class Run, class Isolate>
BatchResult Stage(const std::vector<BoundTarget>& targets, const FrameKey& expected, const Admission& binding,
                  const InferenceKey& inference, std::size_t max_targets, const Thresholds& thresholds,
                  Current&& current, Eligible&& eligible, Run&& run, Isolate&& isolate) noexcept {
    static_assert(std::is_nothrow_invocable_v<Isolate&>, "isolation must be noexcept");
    try {
        if (!ValidKey(expected) || !ValidThresholds(thresholds) ||
            !electric_scene_candidate::ValidAdmission(binding) || binding.scene != thresholds.scene ||
            !inference.valid() || inference.model_sha256 != binding.frozen_model_sha256)
            return {BatchStatus::InvalidConfig, {}, Scene{}, 0.f, {}};
        if (!max_targets || max_targets > 64)
            return {BatchStatus::CapacityExceeded, {}, Scene{}, 0.f, {}};
        if (!current())
            return {BatchStatus::Cancelled, {}, Scene{}, 0.f, {}};
        std::vector<std::pair<std::size_t, Roi>> selected;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto& b = targets[i];
            const auto& t = b.target;
            if (!(b.frame == expected) || !(b.inference == inference))
                return {BatchStatus::InvalidTarget, {}, Scene{}, 0.f, {}};
            if (t.classId != 2 && t.classId != 3)
                continue;
            const auto decision = eligible(t);
            if (decision == Eligibility::Unsupported)
                return {BatchStatus::UnsupportedGraph, {}, Scene{}, 0.f, {}};
            if (decision == Eligibility::Skip)
                continue;
            // Tracker LOSS normally sets bFilter. If an override forces a predicted
            // box into classification, this candidate has not admitted that graph.
            if (t.confidence.source != AiAnnotationSource::Detection)
                return {BatchStatus::UnsupportedGraph, {}, Scene{}, 0.f, {}};
            if (!(b.frame == expected) || t.frameIndex != expected.frame_index || t.targetId.empty() ||
                t.confidence.atomic_code != "2000001" ||
                !((t.classId == 2 && t.confidence.label == "Nonvehicle") ||
                  (t.classId == 3 && t.confidence.label == "Vehicle")) ||
                !std::isfinite(t.confidence.confidence) || t.confidence.confidence < 0.f ||
                t.confidence.confidence > 1.f)
                return {BatchStatus::InvalidTarget, {}, Scene{}, 0.f, {}};
            const auto& box = t.relatedEl.bActive ? t.relatedEl.box : t.box;
            const auto roi  = BoxToRoi(box, 1920, 1080);
            if (!roi)
                return {BatchStatus::InvalidTarget, {}, Scene{}, 0.f, {}};
            for (const auto& prior : selected)
                if (targets[prior.first].target.targetId == t.targetId)
                    return {BatchStatus::InvalidTarget, {}, Scene{}, 0.f, {}};
            selected.emplace_back(i, *roi);
            if (selected.size() > max_targets)
                return {BatchStatus::CapacityExceeded, {}, Scene{}, 0.f, {}};
        }
        std::vector<CompactResult> staged;
        staged.reserve(selected.size());
        for (const auto& item : selected) {
            if (!current())
                return {BatchStatus::Cancelled, {}, Scene{}, 0.f, {}};
            const auto& target = targets[item.first].target;
            CompactResult r{target.targetId,         expected, inference, item.second, {}, false,
                            target.relatedEl.bActive};
            const auto status = run(target, item.second, r.scores);
            if (status == RunStatus::CompletionUnknown) {
                isolate();
                return {BatchStatus::CompletionUnknown, {}, Scene{}, 0.f, {}};
            }
            if (status == RunStatus::Cancelled)
                return {BatchStatus::Cancelled, {}, Scene{}, 0.f, {}};
            if (status != RunStatus::Complete)
                return {BatchStatus::ExecutionFailed, {}, Scene{}, 0.f, {}};
            const auto result = Decide(r.scores, thresholds);
            if (!result)
                return {BatchStatus::ExecutionFailed, {}, Scene{}, 0.f, {}};
            r.scene_unsafe = *result;
            if (!current())
                return {BatchStatus::Cancelled, {}, Scene{}, 0.f, {}};
            staged.push_back(std::move(r));
        }
        if (!current())
            return {BatchStatus::Cancelled, {}, Scene{}, 0.f, {}};
        return {BatchStatus::Complete, std::move(staged), thresholds.scene, thresholds.value, inference};
    } catch (...) {
        isolate();
        return {BatchStatus::CompletionUnknown, {}, Scene{}, 0.f, {}};
    }
}
}  // namespace cosmo::electric_candidate
