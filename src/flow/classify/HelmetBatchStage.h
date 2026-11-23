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

#include "infer/AiCommon.h"

namespace cosmo::helmet_candidate {
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
struct BoundTarget {
    AiDetectRstEl target;
    FrameKey frame;
};
struct Thresholds {
    float helmet{.39f}, safe_helmet{.1f}, hatless{.95f}, unsure{.3f};
};
inline bool ValidThresholds(const Thresholds& t) noexcept {
    for (float x : {t.helmet, t.safe_helmet, t.hatless, t.unsure})
        if (!std::isfinite(x) || x < 0.f || x > 1.f)
            return false;
    return true;
}
inline std::optional<bool> Decide(const std::array<float, 5>& s, const Thresholds& t) noexcept {
    if (!ValidThresholds(t))
        return {};
    for (float x : s)
        if (!std::isfinite(x) || x < 0.f || x > 1.f)
            return {};
    return (s[3] >= t.hatless && s[4] < t.unsure) ||
           (s[3] < t.hatless && s[0] < t.helmet && s[1] < t.safe_helmet && s[4] < t.unsure);
}
inline std::optional<Roi> BoxToRoi(const util::Box& box, int width, int height) noexcept {
    if (width != 1920 || height != 1080 || box.width <= 0 || box.height <= 0)
        return {};
    const std::int64_t x0 = box.x, y0 = box.y, x1 = x0 + static_cast<std::int64_t>(box.width),
                       y1 = y0 + static_cast<std::int64_t>(box.height);
    const auto l = std::clamp<std::int64_t>(x0, 0, width), t = std::clamp<std::int64_t>(y0, 0, height),
               r = std::clamp<std::int64_t>(x1, 0, width), b = std::clamp<std::int64_t>(y1, 0, height);
    if (l >= r || t >= b)
        return {};
    return Roi{static_cast<std::int32_t>(l), static_cast<std::int32_t>(t), static_cast<std::int32_t>(r),
               static_cast<std::int32_t>(b)};
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
    Roi roi{};
    std::array<float, 5> scores{};
    bool scene_unsafe{false};
    bool use_related_box{false};
};
struct BatchResult {
    BatchStatus status{BatchStatus::ExecutionFailed};
    std::vector<CompactResult> outputs;
};
// Caller must supply a task-local post-filter/tracker snapshot with a genuine
// decoder-issued frame key, not an arbitrary key copied from input fields.
// `eligible` must be pure, with no task history/event side effects. `current`
// must check task, model and configuration generations. `run` must retain the
// source/model/IO owners and poison global AX on unknown completion. This
// function returns staged data only: publication needs an atomic combined
// task-and-model commit gate at the existing graph's publication boundary.
template <class Current, class Eligible, class Run, class Isolate>
BatchResult Stage(const std::vector<BoundTarget>& targets, const FrameKey& expected, std::size_t max_targets,
                  const Thresholds& thresholds, Current&& current, Eligible&& eligible, Run&& run,
                  Isolate&& isolate) noexcept {
    static_assert(std::is_nothrow_invocable_v<Isolate&>, "isolation must be noexcept");
    try {
        if (!ValidKey(expected) || !ValidThresholds(thresholds))
            return {BatchStatus::InvalidConfig, {}};
        if (!max_targets)
            return {BatchStatus::CapacityExceeded, {}};
        if (!current())
            return {BatchStatus::Cancelled, {}};
        std::vector<std::pair<std::size_t, Roi>> selected;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto& b = targets[i];
            const auto& t = b.target;
            if (t.classId != 1)
                continue;
            const auto decision = eligible(t);
            if (decision == Eligibility::Unsupported)
                return {BatchStatus::UnsupportedGraph, {}};
            if (decision == Eligibility::Skip)
                continue;
            // Tracker LOSS normally sets bFilter. If an override forces a predicted
            // box into classification, this candidate has not admitted that graph.
            if (t.confidence.source != AiAnnotationSource::Detection)
                return {BatchStatus::UnsupportedGraph, {}};
            if (!(b.frame == expected) || t.frameIndex != expected.frame_index || t.targetId.empty() ||
                t.confidence.atomic_code != "1001003" || t.confidence.label != "pedestrian" ||
                !std::isfinite(t.confidence.confidence) || t.confidence.confidence < 0.f ||
                t.confidence.confidence > 1.f)
                return {BatchStatus::InvalidTarget, {}};
            const auto& box = t.relatedEl.bActive ? t.relatedEl.box : t.box;
            const auto roi  = BoxToRoi(box, 1920, 1080);
            if (!roi)
                return {BatchStatus::InvalidTarget, {}};
            for (const auto& prior : selected)
                if (targets[prior.first].target.targetId == t.targetId)
                    return {BatchStatus::InvalidTarget, {}};
            selected.emplace_back(i, *roi);
            if (selected.size() > max_targets)
                return {BatchStatus::CapacityExceeded, {}};
        }
        std::vector<CompactResult> staged;
        staged.reserve(selected.size());
        for (const auto& item : selected) {
            if (!current())
                return {BatchStatus::Cancelled, {}};
            const auto& target = targets[item.first].target;
            CompactResult r{target.targetId, expected, item.second, {}, false, target.relatedEl.bActive};
            const auto status = run(target, item.second, r.scores);
            if (status == RunStatus::CompletionUnknown) {
                isolate();
                return {BatchStatus::CompletionUnknown, {}};
            }
            if (status == RunStatus::Cancelled)
                return {BatchStatus::Cancelled, {}};
            if (status != RunStatus::Complete)
                return {BatchStatus::ExecutionFailed, {}};
            const auto result = Decide(r.scores, thresholds);
            if (!result)
                return {BatchStatus::ExecutionFailed, {}};
            r.scene_unsafe = *result;
            if (!current())
                return {BatchStatus::Cancelled, {}};
            staged.push_back(std::move(r));
        }
        if (!current())
            return {BatchStatus::Cancelled, {}};
        return {BatchStatus::Complete, std::move(staged)};
    } catch (...) {
        isolate();
        return {BatchStatus::CompletionUnknown, {}};
    }
}
}  // namespace cosmo::helmet_candidate
