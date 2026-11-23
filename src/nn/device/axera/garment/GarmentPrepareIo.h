#pragma once
// Candidate-only descriptor preparation. This checks the submitted bindings,
// never dereferences Y/UV pixels, and does not submit any SDK work.
#include <array>
#include <cstring>
#include <limits>

#include "ax_global_type.h"
#include "nn/device/axera/helmet/HelmetPoolProof.h"
#include "nn/device/axera/helmet/HelmetPrepareIo.h"
namespace cosmo::helmet_candidate {
inline bool PrepareGarmentIo(PendingResources& p, const HelmetPoolProof& proof,
                             const std::array<std::int32_t, 4>& roi) noexcept {
    if (!proof.Matches(p.frame) || !p.frame || !p.frame->Valid() || !p.frame->frame_witness ||
        !p.frame->frame_witness->Matches(*p.frame))
        return false;
    const auto& f = *p.frame;
    if (f.memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        f.format != media::NativeVideoBufferFormat::NV12 || f.width != 1920 || f.height != 1080 || f.crop_x ||
        f.crop_y || f.color_space != media::NativeVideoColorSpace::Bt709 ||
        f.color_range != media::NativeVideoColorRange::Limited || f.planes[0].stride != 2048 ||
        f.planes[1].stride != 2048 || f.planes[0].bytes != 2211840 || f.planes[1].bytes != 1105920 ||
        roi[0] < 0 || roi[1] < 0 || roi[0] >= roi[2] || roi[1] >= roi[3] ||
        (static_cast<std::int64_t>(roi[3]) - roi[1]) < 2 || roi[2] > 1920 || roi[3] > 1080 ||
        !p.mapped_planes[0] || !p.mapped_planes[1])
        return false;
    const auto* d = static_cast<const AX_VIDEO_FRAME_T*>(f.backend_frame);
    if (!d || d->enImgFormat != AX_FORMAT_YUV420_SEMIPLANAR ||
        d->stCompressInfo.enCompressMode != AX_COMPRESS_MODE_NONE || d->u32Width != 1920 ||
        d->u32Height != 1080 || d->u32FrameSize != f.bytes ||
        d->u64PhyAddr[0] != f.planes[0].physical_address ||
        d->u64PhyAddr[1] != f.planes[1].physical_address || d->u32PicStride[0] != 2048 ||
        d->u32PicStride[1] != 2048 || d->s16CropX || d->s16CropY || d->s16CropWidth || d->s16CropHeight ||
        bool(d->u64VirAddr[0]) != bool(d->u64VirAddr[1]))
        return false;
    const std::uint64_t lengths[]{2211840, 1105920};
    std::uint64_t phys[2]{f.planes[0].physical_address, f.planes[1].physical_address};
    std::uint64_t virt[2]{reinterpret_cast<std::uintptr_t>(p.mapped_planes[0]),
                          reinterpret_cast<std::uintptr_t>(p.mapped_planes[1])};
    for (int i = 0; i < 2; ++i) {
        if (!RangeOk(phys[i], lengths[i]) || !RangeOk(virt[i], lengths[i]))
            return false;
        if (d->u64VirAddr[i] && d->u64VirAddr[i] != virt[i])
            return false;
    }
    if (Overlap(phys[0], lengths[0], phys[1], lengths[1]) ||
        Overlap(virt[0], lengths[0], virt[1], lengths[1]))
        return false;
    const AX_U32 sizes[]{16, 1, 264};
    for (int i = 0; i < 3; ++i) {
        const auto& b = p.io_blocks[i];
        auto host     = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(b.host));
        if (b.bytes != sizes[i] || !RangeOk(b.physical, b.bytes) || !RangeOk(host, b.bytes))
            return false;
        for (int j = 0; j < 2; ++j)
            if (Overlap(b.physical, b.bytes, phys[j], lengths[j]) ||
                Overlap(host, b.bytes, virt[j], lengths[j]))
                return false;
        for (int j = 0; j < i; ++j) {
            const auto& prior = p.io_blocks[j];
            auto ph           = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(prior.host));
            if (Overlap(b.physical, b.bytes, prior.physical, prior.bytes) ||
                Overlap(host, b.bytes, ph, prior.bytes))
                return false;
        }
    }
    std::memcpy(p.io_blocks[0].host, roi.data(), 16);
    std::memset(p.io_blocks[1].host, 0, 1);
    std::memset(p.io_blocks[2].host, 0, 264);
    p.y_stride               = {2048, 1};
    p.uv_stride              = {2048, 1};
    p.roi_stride             = {4};
    p.valid_stride           = {1};
    p.scores_stride          = {264, 4};
    p.inputs                 = {};
    p.outputs                = {};
    p.io                     = {};
    p.inputs[0].phyAddr      = phys[0];
    p.inputs[0].pVirAddr     = p.mapped_planes[0];
    p.inputs[0].nSize        = lengths[0];
    p.inputs[0].pStride      = p.y_stride.data();
    p.inputs[0].nStrideSize  = 2;
    p.inputs[1].phyAddr      = phys[1];
    p.inputs[1].pVirAddr     = p.mapped_planes[1];
    p.inputs[1].nSize        = lengths[1];
    p.inputs[1].pStride      = p.uv_stride.data();
    p.inputs[1].nStrideSize  = 2;
    p.inputs[2].phyAddr      = p.io_blocks[0].physical;
    p.inputs[2].pVirAddr     = p.io_blocks[0].host;
    p.inputs[2].nSize        = 16;
    p.inputs[2].pStride      = p.roi_stride.data();
    p.inputs[2].nStrideSize  = 1;
    p.outputs[0].phyAddr     = p.io_blocks[1].physical;
    p.outputs[0].pVirAddr    = p.io_blocks[1].host;
    p.outputs[0].nSize       = 1;
    p.outputs[0].pStride     = p.valid_stride.data();
    p.outputs[0].nStrideSize = 1;
    p.outputs[1].phyAddr     = p.io_blocks[2].physical;
    p.outputs[1].pVirAddr    = p.io_blocks[2].host;
    p.outputs[1].nSize       = 264;
    p.outputs[1].pStride     = p.scores_stride.data();
    p.outputs[1].nStrideSize = 2;
    p.io.pInputs             = p.inputs.data();
    p.io.nInputSize          = 3;
    p.io.pOutputs            = p.outputs.data();
    p.io.nOutputSize         = 2;
    return true;
}
}  // namespace cosmo::helmet_candidate
