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

#include "HelmetBatchStage.h"
namespace cosmo::helmet_candidate {
// Reference .26 config + the repository scenario suggest this order for the
// candidate source ONNX; exact installed-reference weight identity is not yet
// established. Do not register this mapping without an independently admitted
// model/metadata/scene contract.
inline constexpr std::array<const char*, 5> kLabels{"pedHelmet", "pedSafeHelmet", nullptr, "hatless",
                                                    "unsure"};
using TaskOutput = std::vector<AiDetectRstEl>;
// Preserve selected_indices order from the admitted model configuration. The
// four scenario-required labels may not be omitted or duplicated.
struct OutputSelection {
    std::vector<std::size_t> selected{0, 1, 3, 4};
};
inline bool ValidSelection(const OutputSelection& s) noexcept {
    if (s.selected.empty() || s.selected.size() > kLabels.size())
        return false;
    std::array<bool, 5> seen{};
    for (auto k : s.selected) {
        if (k >= seen.size() || k == 2 || seen[k])
            return false;
        seen[k] = true;
    }
    return seen[0] && seen[1] && seen[3] && seen[4];
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
                c.atomic_code   = "2001036";
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
}  // namespace cosmo::helmet_candidate
