#pragma once
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>

#include "ax_ivps_api.h"
#include "ax_sys_api.h"
#include "media/NativeVideoBuffer.h"
#include "nn/device/axera/axera_native_execution_state.h"

namespace cosmo::nn::axera::person720 {
// A derived input is deliberately not given a decoder witness. Its owner
// retains the validated original frame for the full IVPS and model lifetime.
struct ScaledOwner {
    media::NativeVideoBufferPtr original;
    AX_VIDEO_FRAME_T frame{};
    AX_IVPS_ASPECT_RATIO_T aspect{};
    AX_U64 physical{0};
    void* host{nullptr};
    std::thread::id owner_thread{std::this_thread::get_id()};
    std::shared_ptr<ScaledOwner> quarantine;
    ~ScaledOwner() {
        auto& state = detail::NativeState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!physical && !host)
            return;
        if (state.poisoned || owner_thread != std::this_thread::get_id() || !physical || !host ||
            AX_SYS_MemFree(physical, host) != 0)
            state.poisoned = true;
    }
};

inline media::NativeVideoBufferPtr Scale(const media::NativeVideoBufferPtr& source) {
    constexpr AX_U32 stride = 2048, height = 1080, y_bytes = stride * height;
    constexpr AX_U32 uv_bytes = stride * (height / 2), total = y_bytes + uv_bytes;
    if (!source || !source->Valid() || !source->frame_witness || !source->frame_witness->Matches(*source) ||
        source->width != 1280 || source->height != 720 ||
        source->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
        source->format != media::NativeVideoBufferFormat::NV12 || source->crop_x || source->crop_y ||
        source->color_space != media::NativeVideoColorSpace::Bt709 ||
        source->color_range != media::NativeVideoColorRange::Limited)
        return {};
    const auto* input = static_cast<const AX_VIDEO_FRAME_T*>(source->backend_frame);
    if (!input || input->enImgFormat != AX_FORMAT_YUV420_SEMIPLANAR ||
        input->stCompressInfo.enCompressMode != AX_COMPRESS_MODE_NONE || input->u32Width != 1280 ||
        input->u32Height != 720 || input->u32FrameSize != source->bytes ||
        input->u64PhyAddr[0] != source->planes[0].physical_address ||
        input->u64PhyAddr[1] != source->planes[1].physical_address ||
        input->u32PicStride[0] != source->planes[0].stride ||
        input->u32PicStride[1] != source->planes[1].stride || source->planes[0].stride < 1280 ||
        source->planes[1].stride < 1280 ||
        source->planes[0].stride > std::numeric_limits<size_t>::max() / 720 ||
        source->planes[1].stride > std::numeric_limits<size_t>::max() / 360 ||
        source->planes[0].bytes < source->planes[0].stride * 720 ||
        source->planes[1].bytes < source->planes[1].stride * 360 || source->planes[0].bytes > source->bytes ||
        source->planes[1].bytes > source->bytes - source->planes[0].bytes ||
        source->planes[0].physical_address > std::numeric_limits<uint64_t>::max() - source->planes[0].bytes ||
        source->planes[1].physical_address > std::numeric_limits<uint64_t>::max() - source->planes[1].bytes ||
        (source->planes[0].physical_address < source->planes[1].physical_address + source->planes[1].bytes &&
         source->planes[1].physical_address < source->planes[0].physical_address + source->planes[0].bytes))
        return {};
    auto owner      = std::make_shared<ScaledOwner>();
    owner->original = source;
    auto& state     = detail::NativeState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.poisoned)
            return {};
        static bool ivps_initialized = false;
        if (!ivps_initialized) {
            if (AX_IVPS_Init() != 0)
                return {};
            ivps_initialized = true;
        }
        const auto alloc = AX_SYS_MemAlloc(&owner->physical, &owner->host, total, 128,
                                           reinterpret_cast<const AX_S8*>("cosmo-person720"));
        if (alloc != 0 || !owner->physical || !owner->host ||
            owner->physical > std::numeric_limits<AX_U64>::max() - total ||
            reinterpret_cast<std::uintptr_t>(owner->host) >
                std::numeric_limits<std::uintptr_t>::max() - total) {
            if (owner->physical || owner->host) {
                state.poisoned    = true;
                owner->quarantine = owner;
            }
            return {};
        }
        auto& frame                         = owner->frame;
        frame.u32Width                      = 1920;
        frame.u32Height                     = height;
        frame.u32FrameSize                  = total;
        frame.u32PicStride[0]               = stride;
        frame.u32PicStride[1]               = stride;
        frame.enImgFormat                   = AX_FORMAT_YUV420_SEMIPLANAR;
        frame.stCompressInfo.enCompressMode = AX_COMPRESS_MODE_NONE;
        frame.u64PhyAddr[0]                 = owner->physical;
        frame.u64PhyAddr[1]                 = owner->physical + y_bytes;
        frame.u64VirAddr[0]                 = reinterpret_cast<std::uintptr_t>(owner->host);
        frame.u64VirAddr[1]                 = frame.u64VirAddr[0] + y_bytes;
        frame.stColorGamut                  = input->stColorGamut;
        frame.stDynamicRange                = input->stDynamicRange;
        frame.u64PTS                        = input->u64PTS;
        owner->aspect.eMode                 = AX_IVPS_ASPECT_RATIO_STRETCH;
        owner->quarantine                   = owner;
        if (AX_IVPS_CropResizeVpp(input, &frame, &owner->aspect) != 0) {
            state.poisoned = true;
            return {};
        }
        owner->quarantine.reset();
    }
    auto result                      = std::make_shared<media::NativeVideoBuffer>();
    result->memory_kind              = media::NativeVideoMemoryKind::AxeraPhysical;
    result->bytes                    = total;
    result->width                    = 1920;
    result->height                   = height;
    result->width_stride             = stride;
    result->height_stride            = height;
    result->format                   = media::NativeVideoBufferFormat::NV12;
    result->color_space              = source->color_space;
    result->color_range              = source->color_range;
    result->planes[0]                = {owner->physical, y_bytes, stride, 0};
    result->planes[1]                = {owner->physical + y_bytes, uv_bytes, stride, 0};
    result->plane_count              = 2;
    result->backend_frame            = &owner->frame;
    result->owner                    = owner;
    result->frame_index              = source->frame_index;
    result->stream_generation        = source->stream_generation;
    result->decoder_token            = source->decoder_token;
    result->source_pts               = source->source_pts;
    result->source_timestamp         = source->source_timestamp;
    result->source_arrival_timestamp = source->source_arrival_timestamp;
    result->presentation_steady_ms   = source->presentation_steady_ms;
    return result->Valid() ? result : media::NativeVideoBufferPtr{};
}
}  // namespace cosmo::nn::axera::person720
#endif
