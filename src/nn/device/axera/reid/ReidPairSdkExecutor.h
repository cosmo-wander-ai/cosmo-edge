#pragma once
// Experimental two-branch executor. No task/graph registration or enrollment.
// Both models and the native frame remain owned together until both outputs are staged.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#include "ReidMetadata.h"
#include "ReidPrepareIo.h"
#include "nn/device/axera/helmet/HelmetSdkExecutor.h"
namespace cosmo::helmet_candidate {
struct ReidIoOwner final : IoOwner {};
struct ReidPairModelOwner : ModelOwner {
    virtual AX_ENGINE_HANDLE BranchForOwnerThread(unsigned branch) noexcept = 0;
    virtual ReidInputProfile Profile() const noexcept                       = 0;
    virtual AX_ENGINE_HANDLE QualityForOwnerThread() noexcept {
        return nullptr;
    }
};
struct ReidPairResult {
    GuardResult result{GuardResult::InvalidResources};
    std::array<float, 1024> feature{};
    std::vector<std::array<float, 1024>> features;
    std::vector<std::array<float, 6>> quality;
    std::shared_ptr<ManagedPermit<TaskTag>> task_permit;
    std::shared_ptr<ManagedPermit<ModelTag>> model_permit;
};
inline ReidPairResult ExecuteReidBatch(std::unique_ptr<PendingResources> pending,
                                       const std::vector<std::array<std::int32_t, 4>>& rois) {
    ReidPairResult result;
    if (!pending || rois.empty() || rois.size() > 64)
        return result;
    pending->io_owner = std::make_shared<ReidIoOwner>();
    result.features.resize(rois.size());
    auto sdk   = std::dynamic_pointer_cast<HelmetSdk>(pending->sdk);
    auto owner = std::dynamic_pointer_cast<ReidPairModelOwner>(pending->model);
    auto task  = std::dynamic_pointer_cast<ManagedPermit<TaskTag>>(pending->task_permit);
    auto model = std::dynamic_pointer_cast<ManagedPermit<ModelTag>>(pending->model_permit);
    if (!sdk || !owner || !task || !model)
        return result;
    // HandleForOwnerThread locks NativeState.mutex. Capture before the guard owns
    // that mutex; the bundle remains retained by pending for both forwards.
    AX_ENGINE_HANDLE handles[2] = {owner->BranchForOwnerThread(0), owner->BranchForOwnerThread(1)};
    if (!handles[0] || !handles[1] || handles[0] != pending->handle || handles[0] == handles[1])
        return result;
    AX_ENGINE_HANDLE quality_handle = owner->QualityForOwnerThread();
    if (quality_handle && (owner->Profile() != ReidInputProfile::Video709Limited ||
                           quality_handle == handles[0] || quality_handle == handles[1]))
        return result;
    if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return result;
    constexpr AX_U32 sizes[]{16, 1, 2048}, plane_bytes[]{2211840, 1105920};
    bool valid    = false;
    result.result = PendingResourceGuard::Run(
        std::move(pending),
        [&](PendingResources& p) {
            for (auto handle : handles) {
                AX_ENGINE_IO_INFO_T* info = nullptr;
                if (sdk->Info(handle, &info) != 0)
                    return DeviceOperation::CompletionUnknown;
                if (!ReidMetadataMatches(info))
                    return DeviceOperation::KnownFailure;
            }
            if (quality_handle) {
                AX_ENGINE_IO_INFO_T* qi = nullptr;
                if (sdk->Info(quality_handle, &qi) != 0)
                    return DeviceOperation::CompletionUnknown;
                if (!PedQualityMetadataMatches(qi))
                    return DeviceOperation::KnownFailure;
            }
            const auto profile = owner->Profile();
            const auto space = profile == ReidInputProfile::Jfif601Full ? media::NativeVideoColorSpace::Bt601
                                                                        : media::NativeVideoColorSpace::Bt709;
            const auto range = profile == ReidInputProfile::Jfif601Full
                                   ? media::NativeVideoColorRange::Full
                                   : media::NativeVideoColorRange::Limited;
            auto proof       = VerifyHelmetPoolBlock(p.frame, *sdk, space, range);
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
            for (std::size_t item = 0; item < rois.size(); ++item) {
                if (quality_handle) {
                    if (!PrepareReidIo(p, *proof, rois[item], profile))
                        return DeviceOperation::KnownFailure;
                    p.scores_stride    = {24, 4};
                    p.outputs[1].nSize = 24;
                    for (auto& b : p.io_blocks)
                        if (sdk->Flush(b.physical, b.host, b.bytes) != 0)
                            return DeviceOperation::CompletionUnknown;
                    if (sdk->Run(quality_handle, &p.io) != 0)
                        return DeviceOperation::CompletionUnknown;
                    for (int i = 1; i < 3; ++i) {
                        auto& b = p.io_blocks[i];
                        if (sdk->Invalidate(b.physical, b.host, b.bytes) != 0)
                            return DeviceOperation::CompletionUnknown;
                    }
                    if (*static_cast<const std::uint8_t*>(p.io_blocks[1].host) != 1)
                        return DeviceOperation::KnownFailure;
                    std::array<float, 6> scores{};
                    std::memcpy(scores.data(), p.io_blocks[2].host, 24);
                    if (!std::all_of(scores.begin(), scores.end(),
                                     [](float x) { return std::isfinite(x) && x >= 0.f && x <= 1.f; }))
                        return DeviceOperation::KnownFailure;
                    result.quality.push_back(scores);
                }
                for (unsigned branch = 0; branch < 2; ++branch) {
                    if (!PrepareReidIo(p, *proof, rois[item], profile))
                        return DeviceOperation::KnownFailure;
                    for (auto& b : p.io_blocks)
                        if (sdk->Flush(b.physical, b.host, b.bytes) != 0)
                            return DeviceOperation::CompletionUnknown;
                    if (sdk->Run(handles[branch], &p.io) != 0)
                        return DeviceOperation::CompletionUnknown;
                    for (int i = 1; i < 3; ++i) {
                        auto& b = p.io_blocks[i];
                        if (sdk->Invalidate(b.physical, b.host, b.bytes) != 0)
                            return DeviceOperation::CompletionUnknown;
                    }
                    if (*static_cast<const std::uint8_t*>(p.io_blocks[1].host) != 1)
                        return DeviceOperation::KnownFailure;
                    std::array<float, 512> part{};
                    std::memcpy(part.data(), p.io_blocks[2].host, 2048);
                    if (!std::all_of(part.begin(), part.end(), [](float x) { return std::isfinite(x); }))
                        return DeviceOperation::KnownFailure;
                    std::copy(part.begin(), part.end(), result.features[item].begin() + branch * 512);
                }
                double norm2 = 0.0;
                for (float x : result.features[item])
                    norm2 += static_cast<double>(x) * x;
                if (!std::isfinite(norm2) || norm2 <= 0.0)
                    return DeviceOperation::KnownFailure;
                const double norm = std::sqrt(norm2);
                for (float& x : result.features[item])
                    x = static_cast<float>(x / norm);
            }
            valid = true;
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
    if (result.result != GuardResult::Complete || !valid) {
        result.features.clear();
        result.quality.clear();
    } else {
        result.feature      = result.features.front();
        result.task_permit  = task;
        result.model_permit = model;
    }
    return result;
}
inline ReidPairResult ExecuteReidPair(std::unique_ptr<PendingResources> pending,
                                      const std::array<std::int32_t, 4>& roi) {
    return ExecuteReidBatch(std::move(pending), {roi});
}
}  // namespace cosmo::helmet_candidate
