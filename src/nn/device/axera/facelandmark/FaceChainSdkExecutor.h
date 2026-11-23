#pragma once
// Synchronous, native-only face landmark + crop + three feature branches.
// This is an integration candidate: graph and lifecycle wiring is still required.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <vector>

#include "FaceAffine.h"
#include "FaceLandmarkMetadata.h"
#include "FaceLandmarkPrepareIo.h"
#include "nn/device/axera/helmet/HelmetSdkExecutor.h"
namespace cosmo::helmet_candidate {
struct FaceChainOwner final : IoOwner {
    std::array<std::shared_ptr<ModelOwner>, 4> models;
    std::array<AX_ENGINE_HANDLE, 4> handles{};
    Block matrix{}, crop{}, feature{};
};
inline bool FaceTensorMatches(const AX_ENGINE_IOMETA_T& m, const char* name, AX_ENGINE_DATA_TYPE_T type,
                              std::initializer_list<AX_S32> shape, AX_U32 size, int layout, int color) {
    return m.pName && std::strcmp(m.pName, name) == 0 && m.eDataType == type && m.pShape &&
           m.nShapeSize == shape.size() && std::equal(shape.begin(), shape.end(), m.pShape) &&
           m.nSize == size && static_cast<int>(m.eLayout) == layout && static_cast<int>(m.eMemoryType) == 0 &&
           m.nQuantizationValue == 0 && m.pExtraMeta &&
           static_cast<int>(m.pExtraMeta->eColorSpace) == color && !m.pStride;
}
inline bool FaceCropMetadataMatches(const AX_ENGINE_IO_INFO_T* p) {
    return p && p->pInputs && p->pOutputs && p->nInputSize == 3 && p->nOutputSize == 1 &&
           p->nMaxBatchSize == 1 && !p->bDynamicBatchSize &&
           FaceTensorMatches(p->pInputs[0], "y_plane", AX_ENGINE_DT_UINT8, {1080, 2048}, 2211840, 0, 0) &&
           FaceTensorMatches(p->pInputs[1], "uv_plane", AX_ENGINE_DT_UINT8, {540, 2048}, 1105920, 0, 0) &&
           FaceTensorMatches(p->pInputs[2], "matrix", AX_ENGINE_DT_FLOAT32, {6}, 24, 0, 0) &&
           FaceTensorMatches(p->pOutputs[0], "crop_u8", AX_ENGINE_DT_UINT8, {1, 112, 112, 3}, 37632, 0, 0);
}
inline bool FaceFeatureBranchMetadataMatches(const AX_ENGINE_IO_INFO_T* p) {
    return p && p->pInputs && p->pOutputs && p->nInputSize == 1 && p->nOutputSize == 1 &&
           p->nMaxBatchSize == 1 && !p->bDynamicBatchSize &&
           FaceTensorMatches(p->pInputs[0], "input", AX_ENGINE_DT_UINT8, {1, 112, 112, 3}, 37632, 1, 7) &&
           FaceTensorMatches(p->pOutputs[0], "output", AX_ENGINE_DT_FLOAT32, {1, 512}, 2048, 0, 0);
}
struct FaceChainResult {
    GuardResult result{GuardResult::InvalidResources};
    std::vector<std::array<float, 10>> landmarks;
    std::vector<std::array<float, 1536>> features;
    std::shared_ptr<ManagedPermit<TaskTag>> task_permit;
    std::shared_ptr<ManagedPermit<ModelTag>> model_permit;
};
inline FaceChainResult ExecuteFaceChainSdkBatch(std::unique_ptr<PendingResources> pending,
                                                const std::vector<std::array<std::int32_t, 4>>& rois,
                                                FaceInputProfile profile) {
    FaceChainResult result;
    if (!pending || rois.empty() || rois.size() > 64)
        return result;
    result.landmarks.reserve(rois.size());
    result.features.reserve(rois.size());
    auto sdk   = std::dynamic_pointer_cast<HelmetSdk>(pending->sdk);
    auto owner = std::dynamic_pointer_cast<FaceChainOwner>(pending->io_owner);
    auto task  = std::dynamic_pointer_cast<ManagedPermit<TaskTag>>(pending->task_permit);
    auto model = std::dynamic_pointer_cast<ManagedPermit<ModelTag>>(pending->model_permit);
    if (!sdk || !owner || !task || !model)
        return result;
    // All five models are retained by pending through this owner before any work.
    for (std::size_t k = 0; k < 4; ++k)
        if (!owner->models[k] || !owner->handles[k] ||
            owner->models[k]->HandleForOwnerThread() != owner->handles[k])
            return result;
    if (!task->permit.TryBegin() || !model->permit.TryBegin())
        return result;
    const AX_U32 sizes[]{16, 1, 40}, plane_bytes[]{2211840, 1105920};
    bool output_valid = false;
    result.result     = PendingResourceGuard::Run(
        std::move(pending),
        [&](PendingResources& p) {
            AX_ENGINE_IO_INFO_T* info = nullptr;
            if (sdk->Info(p.handle, &info) != 0)
                return DeviceOperation::CompletionUnknown;
            if (!FaceLandmarkMetadataMatches(info))
                return DeviceOperation::KnownFailure;
            for (int k = 0; k < 4; ++k) {
                info = nullptr;
                if (sdk->Info(owner->handles[k], &info) != 0)
                    return DeviceOperation::CompletionUnknown;
                if (!(k == 0 ? FaceCropMetadataMatches(info) : FaceFeatureBranchMetadataMatches(info)))
                    return DeviceOperation::KnownFailure;
            }
            const auto space = profile == FaceInputProfile::Jfif601Full ? media::NativeVideoColorSpace::Bt601
                                                                            : media::NativeVideoColorSpace::Bt709;
            const auto range = profile == FaceInputProfile::Jfif601Full
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
            owner->matrix.bytes  = 24;
            owner->crop.bytes    = 37632;
            owner->feature.bytes = 2048;
            if (sdk->Allocate(&owner->matrix.physical, &owner->matrix.host, owner->matrix.bytes) != 0 ||
                !owner->matrix.physical || !owner->matrix.host)
                return DeviceOperation::CompletionUnknown;
            // Physical intermediate is never read, written, mapped, copied or invalidated by CPU.
            if (AX_SYS_MemAlloc(&owner->crop.physical, &owner->crop.host, owner->crop.bytes, 4096,
                                    reinterpret_cast<const AX_S8*>("face-crop")) != 0 ||
                !owner->crop.physical || !owner->crop.host)
                return DeviceOperation::CompletionUnknown;
            if (sdk->Allocate(&owner->feature.physical, &owner->feature.host, owner->feature.bytes) != 0 ||
                !owner->feature.physical || !owner->feature.host)
                return DeviceOperation::CompletionUnknown;
            for (const auto& roi : rois) {
                std::array<float, 10> landmarks{};
                std::array<float, 1536> feature{};
                if (!PrepareFaceLandmarkIo(p, *proof, roi, profile))
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
                if (*static_cast<const std::uint8_t*>(p.io_blocks[1].host) != 1)
                    return DeviceOperation::KnownFailure;
                std::memcpy(landmarks.data(), p.io_blocks[2].host, 40);
                using namespace cosmo::face_landmark_candidate;
                auto points = Project(roi, landmarks);
                if (!points)
                    return DeviceOperation::KnownFailure;
                auto eyes = Affine(*points, true), four = Affine(*points, false);
                if (!eyes || !four)
                    return DeviceOperation::KnownFailure;
                AX_S32 ys[2] = {2048, 1}, uvs[2] = {2048, 1}, ms[1] = {4}, cs[4] = {37632, 336, 3, 1},
                       fs[2] = {2048, 4};
                for (int stage = 0; stage < 3; ++stage) {
                    const auto& mat = stage == 1 ? *four : *eyes;
                    std::memcpy(owner->matrix.host, mat.data(), 24);
                    if (sdk->Flush(owner->matrix.physical, owner->matrix.host, 24) != 0)
                        return DeviceOperation::CompletionUnknown;
                    AX_ENGINE_IO_BUFFER_T ci[3]{}, co[1]{};
                    ci[0].phyAddr     = p.frame->planes[0].physical_address;
                    ci[0].pVirAddr    = p.mapped_planes[0];
                    ci[0].nSize       = plane_bytes[0];
                    ci[0].pStride     = ys;
                    ci[0].nStrideSize = 2;
                    ci[1].phyAddr     = p.frame->planes[1].physical_address;
                    ci[1].pVirAddr    = p.mapped_planes[1];
                    ci[1].nSize       = plane_bytes[1];
                    ci[1].pStride     = uvs;
                    ci[1].nStrideSize = 2;
                    ci[2].phyAddr     = owner->matrix.physical;
                    ci[2].pVirAddr    = owner->matrix.host;
                    ci[2].nSize       = 24;
                    ci[2].pStride     = ms;
                    ci[2].nStrideSize = 1;
                    co[0].phyAddr     = owner->crop.physical;
                    co[0].pVirAddr    = owner->crop.host;
                    co[0].nSize       = 37632;
                    co[0].pStride     = cs;
                    co[0].nStrideSize = 4;
                    AX_ENGINE_IO_T cio{};
                    cio.pInputs     = ci;
                    cio.nInputSize  = 3;
                    cio.pOutputs    = co;
                    cio.nOutputSize = 1;
                    if (sdk->Run(owner->handles[0], &cio) != 0)
                        return DeviceOperation::CompletionUnknown;
                    AX_ENGINE_IO_BUFFER_T fi[1]{}, fo[1]{};
                    fi[0]             = co[0];
                    fo[0].phyAddr     = owner->feature.physical;
                    fo[0].pVirAddr    = owner->feature.host;
                    fo[0].nSize       = 2048;
                    fo[0].pStride     = fs;
                    fo[0].nStrideSize = 2;
                    AX_ENGINE_IO_T fio{};
                    fio.pInputs     = fi;
                    fio.nInputSize  = 1;
                    fio.pOutputs    = fo;
                    fio.nOutputSize = 1;
                    if (sdk->Run(owner->handles[stage + 1], &fio) != 0)
                        return DeviceOperation::CompletionUnknown;
                    if (sdk->Invalidate(owner->feature.physical, owner->feature.host, 2048) != 0)
                        return DeviceOperation::CompletionUnknown;
                    std::memcpy(feature.data() + stage * 512, owner->feature.host, 2048);
                }
                double norm = 0;
                for (float x : feature) {
                    if (!std::isfinite(x))
                        return DeviceOperation::KnownFailure;
                    norm += double(x) * x;
                }
                if (!(norm > 0) || !std::isfinite(norm))
                    return DeviceOperation::KnownFailure;
                norm = std::sqrt(norm);
                for (float& x : feature)
                    x = float(x / norm);
                result.landmarks.push_back(landmarks);
                result.features.push_back(feature);
            }
            output_valid = true;
            return DeviceOperation::Complete;
        },
        [&](PendingResources& p) {
            for (Block* b : {&owner->feature, &owner->crop, &owner->matrix})
                if (b->physical || b->host) {
                    if (!b->physical || !b->host || sdk->Free(b->physical, b->host) != 0)
                        return false;
                    *b = {};
                }
            for (int i = 2; i >= 0; --i) {
                auto& b = p.io_blocks[i];
                if (b.physical || b.host) {
                    if (!b.physical || !b.host || sdk->Free(b.physical, b.host) != 0)
                        return false;
                    b = {};
                }
            }
            for (int i = 1; i >= 0; --i)
                if (p.mapped_owned[i]) {
                    if (sdk->Unmap(p.mapped_planes[i], plane_bytes[i]) != 0)
                        return false;
                    p.mapped_planes[i] = nullptr;
                    p.mapped_owned[i]  = false;
                }
            return true;
        });
    if (result.result == GuardResult::Complete && output_valid) {
        result.task_permit  = task;
        result.model_permit = model;
    } else {
        result.landmarks.clear();
        result.features.clear();
    }
    return result;
}
}  // namespace cosmo::helmet_candidate
