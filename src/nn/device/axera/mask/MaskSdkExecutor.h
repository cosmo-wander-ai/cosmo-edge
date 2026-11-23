#include <functional>
#pragma once
// Candidate SDK executor only. No graph registration, trusted loader, task
// binding, decoder integration or production deployment is implemented here.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "MaskMetadata.h"
#include "MaskPrepareIo.h"
#include "nn/device/axera/helmet/HelmetSdkExecutor.h"
namespace cosmo::helmet_candidate {
struct MaskSdkResult {
    GuardResult result{GuardResult::InvalidResources};
    std::array<float, 2> scores{};
    std::shared_ptr<ManagedPermit<TaskTag>> task_permit;
    std::shared_ptr<ManagedPermit<ModelTag>> model_permit;
};
struct MaskSdkBatchResult {
    GuardResult result{GuardResult::InvalidResources};
    std::vector<std::array<float, 2>> scores;
    std::shared_ptr<ManagedPermit<TaskTag>> task_permit;
    std::shared_ptr<ManagedPermit<ModelTag>> model_permit;
};
inline MaskSdkBatchResult ExecuteMaskSdkBatch(std::unique_ptr<PendingResources> pending,
                                              const std::vector<std::array<std::int32_t, 4>>& rois) {
    MaskSdkBatchResult result;
    if (!pending || rois.empty() || rois.size() > 64)
        return result;
    result.scores.reserve(rois.size());
    auto sdk   = std::dynamic_pointer_cast<HelmetSdk>(pending->sdk);
    auto task  = std::dynamic_pointer_cast<ManagedPermit<TaskTag>>(pending->task_permit);
    auto model = std::dynamic_pointer_cast<ManagedPermit<ModelTag>>(pending->model_permit);
    if (!sdk || !task || !model || !task->permit.TryBegin() || !model->permit.TryBegin())
        return result;
    // These local references do not contain the only copy: pending owns them.
    // Begin is the operation linearization point. A later stop must defer teardown.
    const AX_U32 sizes[]{16, 1, 8}, plane_bytes[]{2211840, 1105920};
    bool output_valid = false;
    result.result     = PendingResourceGuard::Run(
        std::move(pending),
        [&](PendingResources& p) {
            AX_ENGINE_IO_INFO_T* info = nullptr;
            if (sdk->Info(p.handle, &info) != 0)
                return DeviceOperation::CompletionUnknown;
            if (!MaskMetadataMatches(info))
                return DeviceOperation::KnownFailure;
            auto proof = VerifyHelmetPoolBlock(p.frame, *sdk);
            if (!proof)
                return DeviceOperation::KnownFailure;
            for (int i = 0; i < 2; ++i) {
                p.mapped_planes[i] = sdk->Map(p.frame->planes[i].physical_address, plane_bytes[i]);
                if (!p.mapped_planes[i] || p.mapped_planes[i] == reinterpret_cast<void*>(-1))
                    return DeviceOperation::CompletionUnknown;
                p.mapped_owned[i] = true;
            }
            for (int i = 0; i < 3; ++i) {
                auto& b = p.io_blocks[i];
                b.bytes = sizes[i];
                if (sdk->Allocate(&b.physical, &b.host, b.bytes) != 0 || !b.physical || !b.host)
                    return DeviceOperation::CompletionUnknown;
            }
            for (const auto& roi : rois) {
                if (!PrepareMaskIo(p, *proof, roi))
                    return DeviceOperation::KnownFailure;
                for (auto& b : p.io_blocks)
                    if (sdk->Flush(b.physical, b.host, b.bytes) != 0)
                        return DeviceOperation::CompletionUnknown;
                if (sdk->Run(p.handle, &p.io) != 0)
                    return DeviceOperation::CompletionUnknown;
                for (int i = 1; i < 3; ++i) {
                    auto& b = p.io_blocks[i];
                    if (sdk->Invalidate(b.physical, b.host, b.bytes) != 0)
                        return DeviceOperation::CompletionUnknown;
                }
                const auto valid = *static_cast<const std::uint8_t*>(p.io_blocks[1].host);
                if (valid != 1)
                    return DeviceOperation::KnownFailure;
                std::array<float, 2> scores{};
                std::memcpy(scores.data(), p.io_blocks[2].host, 8);
                if (!std::all_of(scores.begin(), scores.end(),
                                     [](float x) { return std::isfinite(x) && x >= 0.f && x <= 1.f; }))
                    return DeviceOperation::KnownFailure;
                result.scores.push_back(scores);
            }
            output_valid = true;
            return DeviceOperation::Complete;
        },
        [&](PendingResources& p) {
            for (int i = 1; i >= 0; --i)
                if (p.mapped_owned[i]) {
                    if (sdk->Unmap(p.mapped_planes[i], plane_bytes[i]) != 0)
                        return false;
                    p.mapped_planes[i] = nullptr;
                    p.mapped_owned[i]  = false;
                }
            for (int i = 2; i >= 0; --i) {
                auto& b = p.io_blocks[i];
                if (b.physical || b.host) {
                    if (!b.physical || !b.host || sdk->Free(b.physical, b.host) != 0)
                        return false;
                    b = {};
                }
            }
            return true;
        });
    if (result.result != GuardResult::Complete || !output_valid)
        result.scores.clear();
    else {
        result.task_permit  = task;
        result.model_permit = model;
    }
    // Scores are staged only; the caller must perform a combined task/model
    // nonthrowing publication using these still-held permits. Real lifecycle and
    // graph authority are not wired, so this remains a disabled prototype.
    return result;
}
inline MaskSdkResult ExecuteMaskSdk(std::unique_ptr<PendingResources> pending,
                                    const std::array<std::int32_t, 4>& roi) {
    auto batch = ExecuteMaskSdkBatch(std::move(pending), {roi});
    MaskSdkResult result;
    result.result = batch.result;
    if (batch.scores.size() == 1)
        result.scores = batch.scores.front();
    result.task_permit  = std::move(batch.task_permit);
    result.model_permit = std::move(batch.model_permit);
    return result;
}
}  // namespace cosmo::helmet_candidate
