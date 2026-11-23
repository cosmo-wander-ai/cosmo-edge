#pragma once

// Benchmark-only isolated adapter. Model identity is bound by LoadHelmetModel.
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <type_traits>
#include <vector>

#include "BenchHelmetIoCache.h"
#include "nn/device/axera/axera_route_identity.h"
#include "nn/device/axera/benchmark/BenchCachedNv12.h"
#include "nn/device/axera/benchmark/BenchCentralPreparedList.h"
#include "nn/device/axera/benchmark/BenchEngineWorker.h"
#include "nn/device/axera/benchmark/BenchHelmetSnapshotCache.h"
#include "nn/device/axera/benchmark/BenchLegacyHelmet.h"
#include "nn/device/axera/benchmark/BenchPhasedGuard.h"
#include "nn/device/axera/benchmark/BenchPreparedHelmet.h"
#include "nn/device/axera/benchmark/BenchRgbPreprocess.h"
#include "nn/device/axera/benchmark/BenchTurnScheduler.h"
#include "nn/device/axera/helmet/HelmetSdkExecutor.h"

namespace cosmo::nn::axera::face_binding {
class SdkLoadedModel;
}
namespace cosmo::bench_ax {
inline const std::string& HelmetAxSha256() {
    return cosmo::nn::axera::route_identity::Digest("benchmark.helmet_sha256");
}
inline const std::string& LegacyHelmetAxSha256() {
    return cosmo::nn::axera::route_identity::Digest("benchmark.helmet_legacy_sha256");
}
using HelmetIoCache = BenchHelmetIoCache<helmet_candidate::HelmetSdk, helmet_candidate::Block,
                                         std::remove_reference_t<decltype(nn::axera::detail::NativeState())>>;
class VerifiedHelmetModel final : public helmet_candidate::ModelOwner {
public:
    AX_ENGINE_HANDLE HandleForOwnerThread() noexcept override;
    bool IsLegacy() const noexcept {
        return legacy_;
    }
    bool IsCentral() const noexcept {
        return central_;
    }
    std::shared_ptr<CentralPreparedList> CentralList() const noexcept {
        return central_list_;
    }
    Nv12Snapshot* OwnerSnapshot() noexcept {
        return snapshot_.ForOwner();
    }
    // Called only before entering BenchPhasedGuard; HandleForOwnerThread can
    // lock NativeState and must never be called under that guard.
    std::shared_ptr<HelmetIoCache> OwnerIoCache(const std::shared_ptr<helmet_candidate::HelmetSdk>& sdk) {
        if (!HandleForOwnerThread() || !sdk)
            return {};
        if (!io_cache_)
            io_cache_ = HelmetIoCache::Create(sdk, model_, bytes_, nn::axera::detail::NativeState());
        return io_cache_;
    }

private:
    friend std::shared_ptr<VerifiedHelmetModel> LoadHelmetModel(const void*, std::size_t);
    VerifiedHelmetModel(std::shared_ptr<const std::vector<std::uint8_t>>,
                        std::shared_ptr<nn::axera::face_binding::SdkLoadedModel>, bool legacy);
    const std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    const std::shared_ptr<nn::axera::face_binding::SdkLoadedModel> model_;
    const bool legacy_;
    const bool central_;
    const std::shared_ptr<CentralPreparedList> central_list_;
    BenchHelmetSnapshotCache snapshot_;
    std::shared_ptr<HelmetIoCache> io_cache_;
};
std::shared_ptr<VerifiedHelmetModel> LoadHelmetModel(const void*, std::size_t);
struct HelmetIoOwner final : helmet_candidate::IoOwner {
    std::array<AX_S32, 4> in_stride{{150528, 672, 3, 1}};
    std::array<AX_S32, 2> out_stride{{8, 4}};
    std::shared_ptr<HelmetIoCache> cache;
    std::shared_ptr<CentralPreparedList> central_list;
};
inline bool HelmetMetadata(const AX_ENGINE_IO_INFO_T* i) noexcept {
    if (!i || !i->pInputs || !i->pOutputs || i->nInputSize != 1 || i->nOutputSize != 1 ||
        i->nMaxBatchSize != 1 || i->bDynamicBatchSize)
        return false;
    const auto& in  = i->pInputs[0];
    const auto& out = i->pOutputs[0];
    const AX_S32 in_shape[]{1, 224, 224, 3}, out_shape[]{1, 2};
    // SDK metadata strides are element strides; runtime I/O strides are bytes.
    const AX_S32 in_stride[]{150528, 672, 3, 1}, out_stride[]{2, 1};
    return in.pName && std::strcmp(in.pName, "images") == 0 && in.pShape && in.nShapeSize == 4 &&
           std::equal(std::begin(in_shape), std::end(in_shape), in.pShape) && in.nSize == 150528 &&
           in.eDataType == AX_ENGINE_DT_UINT8 && static_cast<int>(in.eLayout) == 1 &&
           (!in.pStride || std::equal(std::begin(in_stride), std::end(in_stride), in.pStride)) && out.pName &&
           std::strcmp(out.pName, "output0") == 0 && out.pShape && out.nShapeSize == 2 &&
           std::equal(std::begin(out_shape), std::end(out_shape), out.pShape) && out.nSize == 8 &&
           out.eDataType == AX_ENGINE_DT_FLOAT32 &&
           (!out.pStride || std::equal(std::begin(out_stride), std::end(out_stride), out.pStride));
}
struct HelmetResult {
    helmet_candidate::GuardResult result{helmet_candidate::GuardResult::InvalidResources};
    // Class 0 helmet; class 1 nohelmet. These are staged probabilities.
    std::vector<std::array<float, 2>> scores;
    std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::TaskTag>> task_permit;
    std::shared_ptr<helmet_candidate::ManagedPermit<helmet_candidate::ModelTag>> model_permit;
};
inline HelmetResult RunHelmet(std::unique_ptr<helmet_candidate::PendingResources> pending,
                              const std::vector<std::array<std::int32_t, 4>>& rois) {
    using namespace helmet_candidate;
    HelmetResult result;
    if (!pending || rois.empty() || rois.size() > 64)
        return result;
    const auto verified_model = std::dynamic_pointer_cast<VerifiedHelmetModel>(pending->model);
    if (!verified_model)
        return result;
    const bool legacy = verified_model->IsLegacy();
    auto io_owner     = std::make_shared<HelmetIoOwner>();
    pending->io_owner = io_owner;
    auto sdk          = std::dynamic_pointer_cast<HelmetSdk>(pending->sdk);
    auto task         = std::dynamic_pointer_cast<ManagedPermit<TaskTag>>(pending->task_permit);
    auto model        = std::dynamic_pointer_cast<ManagedPermit<ModelTag>>(pending->model_permit);
    if (!sdk || !task || !model || !task->permit.TryBegin() || !model->permit.TryBegin())
        return result;
    const char* cache_opt = std::getenv("COSMO_AXERA_BENCH_HELMET_CMM_REUSE");
    const bool reuse_io   = legacy && cache_opt && std::strcmp(cache_opt, "1") == 0;
    std::shared_ptr<HelmetIoCache> cache;
    if (reuse_io) {
        try {
            cache = verified_model->OwnerIoCache(sdk);
        } catch (...) {
            return result;
        }
        if (!cache)
            return result;
        io_owner->cache = cache;
    }
    result.scores.reserve(rois.size());
    constexpr AX_U32 planes[]{2211840, 1105920};
    // SDK memory remains untouched while the device lock is released.
    std::vector<std::uint8_t> private_rgb(150528);
    std::vector<std::uint8_t> legacy_roi;
    const auto rc = BenchPhasedGuard::Run(
        std::move(pending),
        [&](PendingResources& p, auto& cpu_phase) {
            AX_ENGINE_IO_INFO_T* info = nullptr;
            if (sdk->Info(p.handle, &info) != 0)
                return DeviceOperation::CompletionUnknown;
            if (!HelmetMetadata(info))
                return DeviceOperation::KnownFailure;
            const auto proof = VerifyHelmetPoolBlock(p.frame, *sdk);
            if (!proof || !proof->Matches(p.frame))
                return DeviceOperation::KnownFailure;
            const auto& f = *p.frame;
            if (f.width != 1920 || f.height != 1080 || f.crop_x || f.crop_y ||
                f.format != media::NativeVideoBufferFormat::NV12 ||
                f.memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
                f.color_space != media::NativeVideoColorSpace::Bt709 ||
                f.color_range != media::NativeVideoColorRange::Limited || f.planes[0].stride != 2048 ||
                f.planes[1].stride != 2048 || f.planes[0].bytes != planes[0] ||
                f.planes[1].bytes != planes[1])
                return DeviceOperation::KnownFailure;
            for (int n = 0; n < 2; ++n) {
                p.mapped_planes[n] = sdk->Map(f.planes[n].physical_address, planes[n]);
                if (!p.mapped_planes[n] || p.mapped_planes[n] == reinterpret_cast<void*>(-1))
                    return DeviceOperation::CompletionUnknown;
                p.mapped_owned[n] = true;
            }
            const AX_U32 sizes[]{150528, 8};
            if (cache) {
                if (!cache->Prepare(p.io_blocks))
                    return DeviceOperation::CompletionUnknown;
            } else {
                for (int n = 0; n < 2; ++n) {
                    auto& b = p.io_blocks[n];
                    b.bytes = sizes[n];
                    if (sdk->Allocate(&b.physical, &b.host, b.bytes) != 0 || !b.physical || !b.host)
                        return DeviceOperation::CompletionUnknown;
                }
            }
            Nv12View view{static_cast<const std::uint8_t*>(p.mapped_planes[0]),
                          static_cast<const std::uint8_t*>(p.mapped_planes[1]),
                          planes[0],
                          planes[1],
                          1920,
                          1080,
                          2048,
                          2048};
            Nv12Snapshot local_snapshot;
            const char* reuse      = std::getenv("COSMO_AXERA_BENCH_HELMET_SNAPSHOT_REUSE");
            Nv12Snapshot* snapshot = legacy && reuse && std::strcmp(reuse, "1") == 0
                                         ? verified_model->OwnerSnapshot()
                                         : &local_snapshot;
            if (!snapshot || !snapshot->CaptureRegions(view, rois))
                return DeviceOperation::KnownFailure;
            view                     = snapshot->View();
            p.inputs                 = {};
            p.outputs                = {};
            p.io                     = {};
            p.inputs[0].phyAddr      = p.io_blocks[0].physical;
            p.inputs[0].pVirAddr     = p.io_blocks[0].host;
            p.inputs[0].nSize        = sizes[0];
            p.inputs[0].pStride      = io_owner->in_stride.data();
            p.inputs[0].nStrideSize  = 4;
            p.outputs[0].phyAddr     = p.io_blocks[1].physical;
            p.outputs[0].pVirAddr    = p.io_blocks[1].host;
            p.outputs[0].nSize       = sizes[1];
            p.outputs[0].pStride     = io_owner->out_stride.data();
            p.outputs[0].nStrideSize = 2;
            p.io.pInputs             = p.inputs.data();
            p.io.nInputSize          = 1;
            p.io.pOutputs            = p.outputs.data();
            p.io.nOutputSize         = 1;
            if (BenchmarkTurnsEnabled()) {
                for (const auto& r : rois)
                    if (r[0] < 0 || r[1] < 0 || r[2] <= r[0] || r[3] <= r[1] || r[2] > 1920 || r[3] > 1080)
                        return DeviceOperation::KnownFailure;
                const auto witness    = p.frame->frame_witness;
                const auto generation = witness->Generation();
                const auto prepared   = RunPreparedHelmetList(
                    rois.size(), cpu_phase,
                    [&](std::size_t i, std::uint8_t* dst, std::size_t bytes) {
                        const auto& r = rois[i];
                        Mapping mapping;
                        const Rect roi{r[0], r[1], r[2] - r[0], r[3] - r[1]};
                        return legacy ? LegacyHelmetConvert(view, roi, dst, bytes, legacy_roi)
                                        : Convert(view, roi, 224, 224, Resize::Stretch, dst, bytes, mapping);
                    },
                    [&] {
                        return p.frame->frame_witness && p.frame->frame_witness == witness &&
                               witness->Generation() == generation && witness->Matches(*p.frame);
                    },
                    [&](std::size_t, const std::uint8_t* input, std::size_t bytes) {
                        std::memcpy(p.io_blocks[0].host, input, bytes);
                        std::memset(p.io_blocks[1].host, 0, sizes[1]);
                        if (sdk->Flush(p.io_blocks[0].physical, p.io_blocks[0].host, sizes[0]) != 0 ||
                            sdk->Run(p.handle, &p.io) != 0 ||
                            sdk->Invalidate(p.io_blocks[1].physical, p.io_blocks[1].host, sizes[1]) != 0)
                            return PreparedListOutcome::CompletionUnknown;
                        std::array<float, 2> scores{};
                        std::memcpy(scores.data(), p.io_blocks[1].host, 8);
                        if (!std::all_of(scores.begin(), scores.end(),
                                           [](float x) { return std::isfinite(x) && x >= 0.f && x <= 1.f; }))
                            return PreparedListOutcome::KnownFailure;
                        result.scores.push_back(scores);
                        return PreparedListOutcome::Complete;
                    });
                return prepared == PreparedListOutcome::Complete       ? DeviceOperation::Complete
                       : prepared == PreparedListOutcome::KnownFailure ? DeviceOperation::KnownFailure
                                                                       : DeviceOperation::CompletionUnknown;
            }
            for (const auto& r : rois) {
                if (r[0] < 0 || r[1] < 0 || r[2] <= r[0] || r[3] <= r[1] || r[2] > 1920 || r[3] > 1080)
                    return DeviceOperation::KnownFailure;
                Mapping mapping;
                const auto witness    = p.frame->frame_witness;
                const auto generation = witness->Generation();
                const auto phase      = cpu_phase([&] {
                    const Rect roi{r[0], r[1], r[2] - r[0], r[3] - r[1]};
                    return legacy ? LegacyHelmetConvert(view, roi, private_rgb.data(), private_rgb.size(),
                                                             legacy_roi)
                                       : Convert(view, roi, 224, 224, Resize::Stretch, private_rgb.data(),
                                                 private_rgb.size(), mapping);
                });
                if (phase == BenchPhasedGuard::CpuPhaseResult::Poisoned)
                    return DeviceOperation::CompletionUnknown;
                if (phase != BenchPhasedGuard::CpuPhaseResult::Complete || !p.frame->frame_witness ||
                    p.frame->frame_witness != witness || witness->Generation() != generation ||
                    !witness->Matches(*p.frame))
                    return DeviceOperation::KnownFailure;
                // The original task/model permits remain in flight; their gate
                // generations cannot be replaced, and the caller checks them again
                // before committing any classification output.
                std::memcpy(p.io_blocks[0].host, private_rgb.data(), sizes[0]);
                std::memset(p.io_blocks[1].host, 0, sizes[1]);
                if (sdk->Flush(p.io_blocks[0].physical, p.io_blocks[0].host, sizes[0]) != 0 ||
                    sdk->Run(p.handle, &p.io) != 0 ||
                    sdk->Invalidate(p.io_blocks[1].physical, p.io_blocks[1].host, sizes[1]) != 0)
                    return DeviceOperation::CompletionUnknown;
                std::array<float, 2> scores{};
                std::memcpy(scores.data(), p.io_blocks[1].host, 8);
                if (!std::all_of(scores.begin(), scores.end(),
                                 [](float x) { return std::isfinite(x) && x >= 0.f && x <= 1.f; }))
                    return DeviceOperation::KnownFailure;
                result.scores.push_back(scores);
            }
            return DeviceOperation::Complete;
        },
        [&](PendingResources& p) {
            for (int n = 1; n >= 0; --n)
                if (p.mapped_owned[n]) {
                    if (sdk->Unmap(p.mapped_planes[n], planes[n]) != 0)
                        return false;
                    p.mapped_planes[n] = nullptr;
                    p.mapped_owned[n]  = false;
                }
            if (cache) {
                p.io_blocks[0] = {};
                p.io_blocks[1] = {};
            } else
                for (int n = 1; n >= 0; --n) {
                    auto& b = p.io_blocks[n];
                    if (b.physical || b.host) {
                        if (!b.physical || !b.host || sdk->Free(b.physical, b.host) != 0)
                            return false;
                        b = {};
                    }
                }
            return true;
        });
    result.result = rc;
    if (rc != GuardResult::Complete)
        result.scores.clear();
    else {
        result.task_permit  = task;
        result.model_permit = model;
    }
    return result;
}
}  // namespace cosmo::bench_ax
