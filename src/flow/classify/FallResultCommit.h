#pragma once
// Isolated mapping/commit candidate. The real task/model permits and graph
// eligibility must be supplied by a future reviewed owner; no route is enabled.
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "FallBatchStage.h"
namespace cosmo::fall_candidate {
// Reference model label file: index 0 normal, index 1 pedfalldown.
// The production config selects only index 1. This code is not wired to a task.
inline constexpr std::array<const char*, 2> kLabels{"normal", "pedfalldown"};
using TaskOutput = std::vector<AiDetectRstEl>;
struct OutputSelection {
    std::vector<std::size_t> selected{1};
};
inline bool ValidSelection(const OutputSelection& s) noexcept {
    return s.selected.size() == 1 && s.selected[0] == 1;
}
template <class Eligible, class NextId>
std::optional<TaskOutput> Materialize(const std::vector<BoundTarget>& input, const FrameKey& expected,
                                      const BatchResult& batch, const Thresholds& thresholds,
                                      const OutputSelection& selection, Eligible&& eligible,
                                      NextId&& next_id) noexcept {
    try {
        if (!ValidKey(expected) || !ValidThresholds(thresholds) || !ValidSelection(selection) ||
            batch.status != BatchStatus::Complete)
            return {};
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (!(input[i].frame == expected) || input[i].target.frameIndex != expected.frame_index ||
                input[i].target.targetId.empty())
                return {};
            for (std::size_t j = 0; j < i; ++j)
                if (input[i].target.targetId == input[j].target.targetId)
                    return {};
        }
        for (const auto& b : input) {
            const auto count = std::count_if(batch.outputs.begin(), batch.outputs.end(),
                                             [&](const auto& r) { return r.target_id == b.target.targetId; });
            if (b.target.classId != 1) {
                if (count)
                    return {};
                continue;
            }
            const auto e = eligible(b.target);
            if (e == Eligibility::Unsupported)
                return {};
            if ((e == Eligibility::Include && count != 1) || (e == Eligibility::Skip && count != 0))
                return {};
        }
        TaskOutput staged;
        staged.reserve(input.size());
        std::vector<std::string> used_ids;
        for (const auto& b : input) {
            staged.push_back(b.target);
            for (const auto& c : b.target.classifyRst)
                if (!c.measurementId.empty())
                    used_ids.push_back(c.measurementId);
            for (const auto& c : b.target.relatedEl.classifyRst)
                if (!c.measurementId.empty())
                    used_ids.push_back(c.measurementId);
        }
        for (std::size_t i = 0; i < batch.outputs.size(); ++i) {
            const auto& r = batch.outputs[i];
            if (!(r.frame == expected))
                return {};
            for (std::size_t j = 0; j < i; ++j)
                if (batch.outputs[j].target_id == r.target_id)
                    return {};
            auto it = std::find_if(staged.begin(), staged.end(),
                                   [&](const auto& t) { return t.targetId == r.target_id; });
            if (it == staged.end() || it->classId != 1 || it->confidence.atomic_code != "1001003" ||
                it->confidence.label != "pedestrian" ||
                it->confidence.source != AiAnnotationSource::Detection ||
                it->relatedEl.bActive != r.use_related_box)
                return {};
            const auto actual_roi = BoxToRoi(it->relatedEl.bActive ? it->relatedEl.box : it->box, 1920, 1080);
            if (!actual_roi || *actual_roi != r.roi)
                return {};
            const auto decision = Decide(r.scores, thresholds);
            if (!decision || *decision != r.scene_unsafe)
                return {};
            for (std::size_t k : selection.selected) {
                std::string id = next_id();
                if (id.empty() || std::find(used_ids.begin(), used_ids.end(), id) != used_ids.end())
                    return {};
                used_ids.push_back(id);
                AiConfidence c;
                c.atomic_code   = "2002008";
                c.source        = AiAnnotationSource::Classification;
                c.measurementId = std::move(id);
                c.confidence    = r.scores[k];
                c.label         = kLabels[k];
                it->classifyRst.push_back(c);
                if (it->relatedEl.bActive)
                    it->relatedEl.classifyRst.push_back(c);
            }
        }
        return staged;
    } catch (...) {
        return {};
    }
}
// Gates must implement the existing nonblocking one-shot CommitIfCurrent
// contract. Lock order is task then model; the callback must only swap already
// prepared task-local memory. A false inner authorization publishes nothing.
template <class TaskPermit, class ModelLease>
bool CommitPrepared(TaskPermit& task, ModelLease& model, TaskOutput& sink,
                    std::optional<TaskOutput>& prepared) noexcept {
    if (!prepared)
        return false;
    bool model_committed      = false;
    const bool task_committed = task.CommitIfCurrent([&]() noexcept {
        model_committed = model.CommitIfCurrent([&]() noexcept { sink.swap(*prepared); });
    });
    return task_committed && model_committed;
}
}  // namespace cosmo::fall_candidate
