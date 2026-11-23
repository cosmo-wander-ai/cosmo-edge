#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_JPEG_INTERNAL_TEST)
#include "nn/device/axera/axera_face_jpeg_device_backend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include "ax_engine_api.h"
#include "ax_sys_api.h"
#include "ax_venc_api.h"
#include "media/AxeraVencModule.h"
#include "nn/device/axera/axera_face_model_sdk_loader.h"
#include "nn/device/axera/axera_native_execution_state.h"
#include "nn/device/axera/axera_sys.h"

namespace cosmo::nn::axera::face_jpeg_candidate {
namespace {
    constexpr AX_U32 kY = 2048 * 1080, kUv = 2048 * 540;
    constexpr AX_U32 kOutY = 1920 * 1080, kOutUv = 1920 * 540;
    constexpr AX_U32 kCscBytes = kOutY + kOutUv, kJpegBytes = 1920 * 1080 * 3;
    struct Pending {
        std::shared_ptr<Pending> retain;
        media::NativeVideoBufferPtr frame;
        std::shared_ptr<const csc_binding::Capability> capability;
        std::optional<csc_binding::Capability::ExecutionLease> execution;
        std::shared_ptr<face_binding::SdkLoadedModel> model;
        nn::AxeraSysLease sys;
        media::AxeraVencModuleLease venc;
        bool venc_acquired{false};
        void* map_y{nullptr};
        void* map_uv{nullptr};
        AX_U64 csc_phys{0}, jpeg_phys{0};
        void* csc_virt{nullptr};
        void* jpeg_virt{nullptr};
        std::array<std::array<AX_S32, 4>, 4> strides{
            {{kY, kY, 2048, 1}, {kUv, kUv, 2048, 1}, {kOutY, kOutY, 1920, 1}, {kOutUv, kOutUv, 1920, 1}}};
        std::array<AX_ENGINE_IO_BUFFER_T, 2> inputs{}, outputs{};
        AX_ENGINE_IO_T io{};
        AX_JPEG_ENCODE_ONCE_PARAMS_T jpeg{};
    };
    bool Range(AX_U64 address, std::size_t size) {
        return address && size && size <= std::numeric_limits<AX_U64>::max() - address;
    }
    bool Separate(AX_U64 a, std::size_t as, AX_U64 b, std::size_t bs) {
        return Range(a, as) && Range(b, bs) && !(a < b + bs && b < a + as);
    }
    bool BlockContains(AX_BLK block, AX_U64 address, std::size_t size) {
        if (block == AX_INVALID_BLOCKID || !Range(address, size))
            return false;
        const auto base   = AX_POOL_Handle2PhysAddr(block);
        const auto extent = AX_POOL_Handle2BlkSize(block);
        return base && extent && address >= base && address - base <= extent &&
               size <= extent - (address - base);
    }
    bool ValidSource(const media::NativeVideoBuffer& s) {
        if (!s.Valid() || s.memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
            s.format != media::NativeVideoBufferFormat::NV12 || s.width != 1920 || s.height != 1080 ||
            s.width_stride != 2048 || s.height_stride != 1080 || s.crop_x || s.crop_y ||
            s.color_space != media::NativeVideoColorSpace::Bt709 ||
            s.color_range != media::NativeVideoColorRange::Limited || !s.frame_witness ||
            !s.frame_witness->Matches(s) || s.planes[0].stride != 2048 || s.planes[1].stride != 2048 ||
            s.planes[0].bytes != kY || s.planes[1].bytes != kUv || s.bytes < kY + kUv ||
            !Separate(s.planes[0].physical_address, kY, s.planes[1].physical_address, kUv))
            return false;
        const auto* f = static_cast<const AX_VIDEO_FRAME_T*>(s.backend_frame);
        return f && f->enImgFormat == AX_FORMAT_YUV420_SEMIPLANAR &&
               f->stCompressInfo.enCompressMode == AX_COMPRESS_MODE_NONE && f->u32Width == 1920 &&
               f->u32Height == 1080 && f->u32FrameSize == s.bytes && f->u32PicStride[0] == 2048 &&
               f->u32PicStride[1] == 2048 && f->u64PhyAddr[0] == s.planes[0].physical_address &&
               f->u64PhyAddr[1] == s.planes[1].physical_address && f->u32BlkId[0] == s.planes[0].block_id &&
               f->u32BlkId[1] == s.planes[1].block_id && f->s16CropX == 0 && f->s16CropY == 0 &&
               (!f->s16CropWidth || f->s16CropWidth == 1920) &&
               (!f->s16CropHeight || f->s16CropHeight == 1080) &&
               BlockContains(f->u32BlkId[0], f->u64PhyAddr[0], kY) &&
               BlockContains(f->u32BlkId[1], f->u64PhyAddr[1], kUv);
    }
    bool Metadata(const AX_ENGINE_IO_INFO_T* info, const csc_binding::Contract& c) {
        if (!info || info->nInputSize != 2 || info->nOutputSize != 2 || !info->pInputs || !info->pOutputs ||
            info->nMaxBatchSize != 1 || info->bDynamicBatchSize)
            return false;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& m       = i < 2 ? info->pInputs[i] : info->pOutputs[i - 2];
            const AX_S32 height = (i == 0 || i == 2) ? 1080 : 540;
            const AX_S32 width  = i < 2 ? 2048 : 1920;
            const std::array<AX_S32, 4> shape{1, 1, height, width};
            const std::array<AX_S32, 4> stride{height * width, height * width, width, 1};
            if (!m.pName || strnlen(m.pName, 1024) == 1024 || c.names[i] != m.pName || !m.pShape ||
                m.nShapeSize != 4 || !std::equal(shape.begin(), shape.end(), m.pShape) ||
                m.nSize != static_cast<AX_U32>(height * width) || m.eDataType != AX_ENGINE_DT_UINT8 ||
                m.eLayout != (i < 2 ? AX_ENGINE_TENSOR_LAYOUT_NCHW : AX_ENGINE_TENSOR_LAYOUT_UNKNOWN) ||
                m.eMemoryType != AX_ENGINE_MT_PHYSICAL || m.nQuantizationValue || !m.pExtraMeta ||
                m.pExtraMeta->eColorSpace != (i < 2 ? AX_ENGINE_CS_GRAY : AX_ENGINE_CS_FEATUREMAP) ||
                (m.pStride ? !std::equal(stride.begin(), stride.end(), m.pStride)
                           : c.model_stride_rule !=
                                 face_binding::ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo))
                return false;
        }
        return true;
    }
    // Called under NativeState mutex. Never destroy Pending here: its model
    // deleter also locks NativeState. Any SDK cleanup ambiguity keeps the whole
    // object self-retained for process lifetime, including frame and SDK owners.
    bool Cleanup(Pending& p) {
        if (p.venc_acquired && !p.venc.BeginJpegCleanup())
            return false;
        const auto failed = [&]() {
            if (p.venc_acquired)
                (void)p.venc.FinishJpegCleanup(false);
            return false;
        };
        if (p.jpeg_phys || p.jpeg_virt) {
            if (!p.jpeg_phys || !p.jpeg_virt || AX_SYS_MemFree(p.jpeg_phys, p.jpeg_virt) != 0)
                return failed();
            p.jpeg_phys = 0;
            p.jpeg_virt = nullptr;
        }
        if (p.csc_phys || p.csc_virt) {
            if (!p.csc_phys || !p.csc_virt || AX_SYS_MemFree(p.csc_phys, p.csc_virt) != 0)
                return failed();
            p.csc_phys = 0;
            p.csc_virt = nullptr;
        }
        if (p.map_uv) {
            if (AX_SYS_Munmap(p.map_uv, kUv) != 0)
                return failed();
            p.map_uv = nullptr;
        }
        if (p.map_y) {
            if (AX_SYS_Munmap(p.map_y, kY) != 0)
                return failed();
            p.map_y = nullptr;
        }
        return !p.venc_acquired || p.venc.FinishJpegCleanup(true);
    }
}  // namespace

BackendResult DeviceBackend::ExecuteAndDiscard(media::NativeVideoBufferPtr frame) noexcept {
    if (!capability_ || !frame)
        return BackendResult::FailedSafe;
    std::shared_ptr<Pending> pending;
    try {
        {
            auto& state = detail::NativeState();
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.poisoned)
                return BackendResult::Quarantined;
        }
        pending             = std::make_shared<Pending>();
        pending->retain     = pending;
        pending->frame      = std::move(frame);
        pending->capability = capability_;
        pending->execution.emplace(capability_->TryAcquireForExecution());
        if (!*pending->execution) {
            pending->retain.reset();
            auto& state = detail::NativeState();
            std::lock_guard<std::mutex> lock(state.mutex);
            return state.poisoned ? BackendResult::Quarantined : BackendResult::FailedSafe;
        }
        pending->model = std::dynamic_pointer_cast<face_binding::SdkLoadedModel>(pending->execution->Model());
        if (!pending->model) {
            pending->retain.reset();
            auto& state = detail::NativeState();
            std::lock_guard<std::mutex> lock(state.mutex);
            return state.poisoned ? BackendResult::Quarantined : BackendResult::FailedSafe;
        }
        const auto handle = pending->model->HandleForOwnerThread();
        if (!handle) {
            pending->retain.reset();
            auto& state = detail::NativeState();
            std::lock_guard<std::mutex> lock(state.mutex);
            return state.poisoned ? BackendResult::Quarantined : BackendResult::FailedSafe;
        }
        auto& state      = detail::NativeState();
        bool success     = false;
        bool quarantined = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            bool unsafe = false;
            do {
                if (state.poisoned) {
                    quarantined = true;
                    break;
                }
                if (!ValidSource(*pending->frame))
                    break;
                AX_ENGINE_IO_INFO_T* info = nullptr;
                if (AX_ENGINE_GetIOInfo(handle, &info) != 0 || !Metadata(info, capability_->GetContract()))
                    break;
                if (pending->sys.Acquire() != 0) {
                    // The shared SYS lease does not attest failed-init cleanup.
                    unsafe = true;
                    break;
                }
                if (!pending->venc.Acquire(media::AxeraVencModuleLease::Mode::Jpeg)) {
                    unsafe = pending->venc.RequiresQuarantine();
                    break;
                }
                pending->venc_acquired = true;
                const auto& f          = *pending->frame;
                pending->map_y         = AX_SYS_Mmap(f.planes[0].physical_address, kY);
                if (pending->map_y == reinterpret_cast<void*>(-1))
                    pending->map_y = nullptr;
                if (!pending->map_y)
                    break;
                pending->map_uv = AX_SYS_Mmap(f.planes[1].physical_address, kUv);
                if (pending->map_uv == reinterpret_cast<void*>(-1))
                    pending->map_uv = nullptr;
                if (!pending->map_uv)
                    break;
                const auto csc_rc = AX_SYS_MemAlloc(&pending->csc_phys, &pending->csc_virt, kCscBytes, 4096,
                                                    reinterpret_cast<const AX_S8*>("cosmo-csc"));
                if (csc_rc != 0 || !pending->csc_phys || !pending->csc_virt) {
                    // SDK does not prove failed/contradictory allocation leaves
                    // no resource; retain all owners rather than guess.
                    unsafe = true;
                    break;
                }
                const auto jpeg_rc = AX_SYS_MemAlloc(&pending->jpeg_phys, &pending->jpeg_virt, kJpegBytes,
                                                     4096, reinterpret_cast<const AX_S8*>("cosmo-jpeg"));
                if (jpeg_rc != 0 || !pending->jpeg_phys || !pending->jpeg_virt) {
                    unsafe = true;
                    break;
                }
                const auto yp = f.planes[0].physical_address, up = f.planes[1].physical_address;
                const auto cp = pending->csc_phys, jp = pending->jpeg_phys;
                if (!Separate(cp, kCscBytes, jp, kJpegBytes) || !Separate(cp, kCscBytes, yp, kY) ||
                    !Separate(cp, kCscBytes, up, kUv) || !Separate(jp, kJpegBytes, yp, kY) ||
                    !Separate(jp, kJpegBytes, up, kUv)) {
                    unsafe = true;
                    break;
                }
                if (reinterpret_cast<std::uintptr_t>(pending->csc_virt) >
                        std::numeric_limits<std::uintptr_t>::max() - kCscBytes ||
                    reinterpret_cast<std::uintptr_t>(pending->jpeg_virt) >
                        std::numeric_limits<std::uintptr_t>::max() - kJpegBytes) {
                    unsafe = true;
                    break;
                }
                auto* csc_bytes              = static_cast<unsigned char*>(pending->csc_virt);
                pending->inputs[0].phyAddr   = yp;
                pending->inputs[0].pVirAddr  = pending->map_y;
                pending->inputs[1].phyAddr   = up;
                pending->inputs[1].pVirAddr  = pending->map_uv;
                pending->outputs[0].phyAddr  = cp;
                pending->outputs[0].pVirAddr = csc_bytes;
                pending->outputs[1].phyAddr  = cp + kOutY;
                pending->outputs[1].pVirAddr = csc_bytes + kOutY;
                const std::array<AX_U32, 4> sizes{kY, kUv, kOutY, kOutUv};
                for (std::size_t i = 0; i < 4; ++i) {
                    auto& b       = i < 2 ? pending->inputs[i] : pending->outputs[i - 2];
                    b.nSize       = sizes[i];
                    b.pStride     = pending->strides[i].data();
                    b.nStrideSize = 4;
                }
                pending->io.pInputs     = pending->inputs.data();
                pending->io.nInputSize  = 2;
                pending->io.pOutputs    = pending->outputs.data();
                pending->io.nOutputSize = 2;
                if (AX_ENGINE_RunSync(handle, &pending->io) != 0) {
                    unsafe = true;
                    break;
                }
                auto& j           = pending->jpeg;
                j.u32Width        = 1920;
                j.u32Height       = 1080;
                j.enImgFormat     = AX_FORMAT_YUV420_SEMIPLANAR;
                j.u32PicStride[0] = j.u32PicStride[1] = 1920;
                j.u64PhyAddr[0]                       = cp;
                j.u64PhyAddr[1]                       = cp + kOutY;
                j.u64VirAddr[0]                       = reinterpret_cast<AX_U64>(csc_bytes);
                j.u64VirAddr[1]                       = reinterpret_cast<AX_U64>(csc_bytes + kOutY);
                j.ulPhyAddr                           = jp;
                j.pu8Addr                             = static_cast<AX_U8*>(pending->jpeg_virt);
                j.u32Len                              = kJpegBytes;
                j.enStrmBufType                       = AX_STREAM_BUF_NON_CACHE;
                j.stJpegParam.u32Qfactor              = 95;
                if (AX_VENC_JpegEncodeOneFrame(&j) != 0) {
                    unsafe = true;
                    break;
                }
                if (j.ulPhyAddr != jp || j.pu8Addr != pending->jpeg_virt || j.u32Len < 4 ||
                    j.u32Len > kJpegBytes) {
                    unsafe = true;
                    break;
                }
                // Only the compressed buffer is read; no pixel buffer is read or copied by CPU.
                const auto* bytes = static_cast<const unsigned char*>(pending->jpeg_virt);
                success           = bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[j.u32Len - 2] == 0xff &&
                          bytes[j.u32Len - 1] == 0xd9;
            } while (false);
            if (!unsafe && !Cleanup(*pending))
                unsafe = true;
            if (unsafe) {
                success        = false;
                quarantined    = true;
                state.poisoned = true;
                pending->venc.Quarantine();
            } else
                pending->retain.reset();
        }
        // On poison, retain both capability and model indefinitely; releasing
        // the mutex on this owner thread lets Binding::Revoke complete. Every
        // SDK handle/loader entry rechecks NativeState before future use.
        pending->execution.reset();
        // Release model ownership only after unlocking NativeState.
        pending.reset();
        return quarantined ? BackendResult::Quarantined
               : success   ? BackendResult::Completed
                           : BackendResult::FailedSafe;
    } catch (...) {
        if (pending) {
            // State may be partially submitted; keep everything alive.
            {
                auto& state = detail::NativeState();
                std::lock_guard<std::mutex> lock(state.mutex);
                state.poisoned = true;
                pending->venc.Quarantine();
            }
            pending->execution.reset();
        }
        return pending ? BackendResult::Quarantined : BackendResult::FailedSafe;
    }
}
}  // namespace cosmo::nn::axera::face_jpeg_candidate
#endif
