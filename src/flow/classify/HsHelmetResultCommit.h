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

#include "HsHelmetBatchStage.h"
namespace cosmo::hshelmet_candidate {
// Emit only the scene-specific confidence label; these semantic names are
// distinct from external scene label IDs and tensor channel numbers.
using TaskOutput = std::vector<AiDetectRstEl>;
template <class Eligible, class NextId>
std::optional<TaskOutput> Materialize(const std::vector<BoundTarget>& input, const FrameKey& expected,
                                      const BatchResult& batch, const Admission& binding,
                                      const InferenceKey& inference, const Thresholds& thresholds,
                                      Eligible&& eligible, NextId&& next_id) noexcept {
    try {
        if (!ValidKey(expected) || !ValidThresholds(thresholds) ||
            !hshelmet_scene_candidate::ValidAdmission(binding) || binding.scene != thresholds.scene ||
            !inference.valid() || inference.model_sha256 != binding.frozen_model_sha256 ||
            !(batch.inference == inference) || batch.status != BatchStatus::Complete ||
            batch.scene != thresholds.scene || batch.threshold != thresholds.value)
            return {};
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (!(input[i].frame == expected) || !(input[i].inference == inference) ||
                input[i].target.frameIndex != expected.frame_index || input[i].target.targetId.empty())
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
        const auto* chosen = hshelmet_scene_candidate::Lookup(thresholds.scene);
        if (!chosen)
            return {};
        // LogicCalcEngine takes the first matching label. Reject preexisting copies
        // so a stale class score cannot shadow the new task-local measurement.
        for (const auto& b : input) {
            for (const auto& c : b.target.classifyRst)
                if (c.label == chosen->output_label)
                    return {};
            for (const auto& c : b.target.relatedEl.classifyRst)
                if (c.label == chosen->output_label)
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
            if (!(r.frame == expected) || !(r.inference == inference))
                return {};
            for (std::size_t j = 0; j < i; ++j)
                if (batch.outputs[j].target_id == r.target_id)
                    return {};
            auto it = std::find_if(staged.begin(), staged.end(),
                                   [&](const auto& t) { return t.targetId == r.target_id; });
            if (it == staged.end() || it->classId != 1 || it->confidence.atomic_code != "1001001" ||
                it->confidence.label != "headShoulder" ||
                it->confidence.source != AiAnnotationSource::Detection ||
                it->relatedEl.bActive != r.use_related_box)
                return {};
            const auto actual_roi = BoxToRoi(it->relatedEl.bActive ? it->relatedEl.box : it->box, 1920, 1080);
            if (!actual_roi || *actual_roi != r.roi)
                return {};
            const auto decision = Decide(r.scores, thresholds);
            if (!decision || *decision != r.scene_unsafe)
                return {};
            const auto* spec = hshelmet_scene_candidate::Lookup(thresholds.scene);
            if (!spec)
                return {};
            std::string id = next_id();
            if (id.empty() || std::find(used_ids.begin(), used_ids.end(), id) != used_ids.end())
                return {};
            used_ids.push_back(id);
            AiConfidence c;
            c.atomic_code   = "2001002";
            c.source        = AiAnnotationSource::Classification;
            c.measurementId = std::move(id);
            c.confidence    = r.scores[spec->tensor_channel];
            c.label         = std::string(spec->output_label);
            it->classifyRst.push_back(c);
            if (it->relatedEl.bActive)
                it->relatedEl.classifyRst.push_back(c);
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
}  // namespace cosmo::hshelmet_candidate
