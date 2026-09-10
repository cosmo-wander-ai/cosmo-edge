#include "media/VideoDecoderRockchip.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <thread>

#define MODULE_TAG "cosmo_mpp_decoder"
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_vdec_cfg.h>

#include "media/PreviewPipelineMetrics.h"
#include "media/RockchipRgaBuffer.h"
#include "media/VideoDecoderCpu.h"
#include "util/Log.h"

namespace cosmo::media {
namespace {

    constexpr RK_U32 kDecoderBufferCount = 24;
    constexpr int kPacketSubmitAttempts  = 30;
    constexpr auto kPacketSubmitWait     = std::chrono::milliseconds(1);
    constexpr size_t kMaxPendingTimings  = 256;

    struct MppFrameHolder {
        explicit MppFrameHolder(MppFrame value) : frame(value) {}

        ~MppFrameHolder() {
            if (frame) {
                mpp_frame_deinit(&frame);
            }
        }

        MppFrame frame{nullptr};
    };

    MppCodingType ToMppCoding(VideoCodecType type) {
        if (type == VideoCodecType::kH264) {
            return MPP_VIDEO_CodingAVC;
        }
        if (type == VideoCodecType::kH265) {
            return MPP_VIDEO_CodingHEVC;
        }
        return MPP_VIDEO_CodingUnused;
    }

    uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point started) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started)
                .count());
    }

    bool IsCompact420Format(MppFrameFormat format) {
        const auto properties = static_cast<RK_U32>(format) & MPP_FRAME_FMT_PROP_MASK;
        if ((properties & (MPP_FRAME_FBC_MASK | MPP_FRAME_TILE_FLAG)) != 0 ||
            MPP_FRAME_FMT_IS_YUV_10BIT(format)) {
            return false;
        }
        const auto base = static_cast<RK_U32>(format) & MPP_FRAME_FMT_MASK;
        return base == MPP_FMT_YUV420P || base == MPP_FMT_YUV420SP || base == MPP_FMT_YUV420SP_VU;
    }

    int ToRga420Format(MppFrameFormat format) {
        const auto base = static_cast<RK_U32>(format) & MPP_FRAME_FMT_MASK;
        if (base == MPP_FMT_YUV420SP) {
            return RK_FORMAT_YCbCr_420_SP;
        }
        if (base == MPP_FMT_YUV420SP_VU) {
            return RK_FORMAT_YCrCb_420_SP;
        }
        if (base == MPP_FMT_YUV420P) {
            return RK_FORMAT_YCbCr_420_P;
        }
        return RK_FORMAT_UNKNOWN;
    }

    NativeVideoColorSpace ToNativeColorSpace(MppFrameColorSpace color_space) {
        switch (color_space) {
            case MPP_FRAME_SPC_BT709:
                return NativeVideoColorSpace::Bt709;
            case MPP_FRAME_SPC_BT470BG:
            case MPP_FRAME_SPC_SMPTE170M:
            case MPP_FRAME_SPC_SMPTE240M:
                return NativeVideoColorSpace::Bt601;
            case MPP_FRAME_SPC_BT2020_NCL:
            case MPP_FRAME_SPC_BT2020_CL:
                return NativeVideoColorSpace::Bt2020;
            default:
                return NativeVideoColorSpace::Unspecified;
        }
    }

    NativeVideoColorRange ToNativeColorRange(MppFrameColorRange color_range) {
        if (color_range == MPP_FRAME_RANGE_MPEG) {
            return NativeVideoColorRange::Limited;
        }
        if (color_range == MPP_FRAME_RANGE_JPEG) {
            return NativeVideoColorRange::Full;
        }
        return NativeVideoColorRange::Unspecified;
    }

    NativeVideoBufferPtr ExportMppBuffer(const std::string& decoder_name, MppFrame frame) {
        if (!frame) {
            return nullptr;
        }
        auto buffer                    = mpp_frame_get_buffer(frame);
        const auto format              = mpp_frame_get_fmt(frame);
        const auto base_format         = static_cast<RK_U32>(format) & MPP_FRAME_FMT_MASK;
        const size_t width             = mpp_frame_get_width(frame);
        const size_t height            = mpp_frame_get_height(frame);
        const size_t horizontal_stride = mpp_frame_get_hor_stride(frame);
        const size_t vertical_stride   = mpp_frame_get_ver_stride(frame);
        if (!buffer || !IsCompact420Format(format) || width == 0 || height == 0 ||
            width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            height > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            horizontal_stride > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            vertical_stride > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            horizontal_stride < width || vertical_stride < height) {
            return nullptr;
        }

        NativeVideoBufferFormat native_format = NativeVideoBufferFormat::Unknown;
        if (base_format == MPP_FMT_YUV420SP) {
            native_format = NativeVideoBufferFormat::NV12;
        } else if (base_format == MPP_FMT_YUV420SP_VU) {
            native_format = NativeVideoBufferFormat::NV21;
        } else if (base_format == MPP_FMT_YUV420P) {
            native_format = NativeVideoBufferFormat::I420;
        }
        const int fd = mpp_buffer_get_fd(buffer);
        if (native_format == NativeVideoBufferFormat::Unknown || fd < 0 ||
            mpp_buffer_inc_ref(buffer) != MPP_OK) {
            LOG_WARN("{} could not retain MPP DMA-BUF for inference", decoder_name);
            return nullptr;
        }

        auto result           = std::make_shared<NativeVideoBuffer>();
        result->fd            = fd;
        result->bytes         = mpp_buffer_get_size(buffer);
        result->width         = static_cast<int>(width);
        result->height        = static_cast<int>(height);
        result->width_stride  = static_cast<int>(horizontal_stride);
        result->height_stride = static_cast<int>(vertical_stride);
        result->format        = native_format;
        result->color_space   = ToNativeColorSpace(mpp_frame_get_colorspace(frame));
        result->color_range   = ToNativeColorRange(mpp_frame_get_color_range(frame));
        result->owner         = std::shared_ptr<void>(buffer, [](void* value) {
            if (value) {
                mpp_buffer_put(static_cast<MppBuffer>(value));
            }
        });
        return result;
    }

    VideoFramePtr CopyMppFrameCpu(const std::string& decoder_name, MppFrame frame) {
        if (!frame) {
            return nullptr;
        }

        const size_t width             = mpp_frame_get_width(frame);
        const size_t height            = mpp_frame_get_height(frame);
        const size_t horizontal_stride = mpp_frame_get_hor_stride(frame);
        const size_t vertical_stride   = mpp_frame_get_ver_stride(frame);
        const auto format              = mpp_frame_get_fmt(frame);
        auto buffer                    = mpp_frame_get_buffer(frame);
        if (width == 0 || height == 0 || width % 2 != 0 || height % 2 != 0 || horizontal_stride < width ||
            vertical_stride < height || !IsCompact420Format(format) || !buffer) {
            LOG_WARN("{} MPP decoder rejected output layout: {}x{} stride={}x{} format=0x{:x}", decoder_name,
                     width, height, horizontal_stride, vertical_stride, static_cast<RK_U32>(format));
            return nullptr;
        }
        if (horizontal_stride > std::numeric_limits<size_t>::max() / vertical_stride) {
            return nullptr;
        }
        const size_t y_plane_size = horizontal_stride * vertical_stride;
        if (y_plane_size > std::numeric_limits<size_t>::max() - y_plane_size / 2) {
            return nullptr;
        }
        const size_t required = y_plane_size * 3 / 2;
        if (mpp_buffer_get_size(buffer) < required) {
            LOG_WARN("{} MPP decoder output buffer too small: {} < {}", decoder_name,
                     mpp_buffer_get_size(buffer), required);
            return nullptr;
        }

        auto output = std::make_shared<VideoFrame>(static_cast<int>(width), static_cast<int>(height),
                                                   PixelFormat::PIXEL_I420);
        if (!output || !output->Active() || !output->GetData()) {
            LOG_WARN("{} compact I420 VideoFrame allocation failed for {}x{}", decoder_name, width, height);
            return nullptr;
        }

        auto* source = static_cast<const uint8_t*>(mpp_buffer_get_ptr(buffer));
        if (!source || mpp_buffer_sync_ro_begin(buffer) != MPP_OK) {
            return nullptr;
        }
        struct ReadSyncGuard {
            MppBuffer buffer;
            ~ReadSyncGuard() {
                mpp_buffer_sync_ro_end(buffer);
            }
        } sync_guard{buffer};

        auto* destination       = output->GetData();
        const size_t compact_y  = width * height;
        const size_t compact_uv = compact_y / 4;
        auto* destination_u     = destination + compact_y;
        auto* destination_v     = destination_u + compact_uv;
        for (size_t row = 0; row < height; ++row) {
            std::memcpy(destination + row * width, source + row * horizontal_stride, width);
        }

        const auto base_format = static_cast<RK_U32>(format) & MPP_FRAME_FMT_MASK;
        if (base_format == MPP_FMT_YUV420P) {
            const size_t chroma_stride = horizontal_stride / 2;
            const size_t chroma_height = vertical_stride / 2;
            const auto* source_u       = source + y_plane_size;
            const auto* source_v       = source_u + chroma_stride * chroma_height;
            for (size_t row = 0; row < height / 2; ++row) {
                std::memcpy(destination_u + row * (width / 2), source_u + row * chroma_stride, width / 2);
                std::memcpy(destination_v + row * (width / 2), source_v + row * chroma_stride, width / 2);
            }
        } else {
            const bool vu_order       = base_format == MPP_FMT_YUV420SP_VU;
            const auto* source_chroma = source + y_plane_size;
            for (size_t row = 0; row < height / 2; ++row) {
                const auto* source_row = source_chroma + row * horizontal_stride;
                auto* u_row            = destination_u + row * (width / 2);
                auto* v_row            = destination_v + row * (width / 2);
                for (size_t column = 0; column < width / 2; ++column) {
                    const auto first  = source_row[column * 2];
                    const auto second = source_row[column * 2 + 1];
                    u_row[column]     = vu_order ? second : first;
                    v_row[column]     = vu_order ? first : second;
                }
            }
        }

        return output;
    }

    VideoFramePtr MaterializeMppFrame(const std::string& decoder_name, MppFrame frame) {
        if (!frame) {
            return nullptr;
        }

        const size_t width             = mpp_frame_get_width(frame);
        const size_t height            = mpp_frame_get_height(frame);
        const size_t horizontal_stride = mpp_frame_get_hor_stride(frame);
        const size_t vertical_stride   = mpp_frame_get_ver_stride(frame);
        const auto format              = mpp_frame_get_fmt(frame);
        auto buffer                    = mpp_frame_get_buffer(frame);
        const int source_format        = ToRga420Format(format);
        if (width == 0 || height == 0 || width % 2 != 0 || height % 2 != 0 ||
            width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            height > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            horizontal_stride > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            vertical_stride > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            horizontal_stride < width || vertical_stride < height || !IsCompact420Format(format) || !buffer ||
            source_format == RK_FORMAT_UNKNOWN) {
            GetPreviewPipelineMetrics().RecordMppRgaCopyOut(false);
            GetPreviewPipelineMetrics().RecordMppCpuCopyOutFallback();
            return CopyMppFrameCpu(decoder_name, frame);
        }

        auto output = std::make_shared<VideoFrame>(static_cast<int>(width), static_cast<int>(height),
                                                   PixelFormat::PIXEL_I420);
        if (!output || !output->Active() || !output->GetData()) {
            GetPreviewPipelineMetrics().RecordMppRgaCopyOut(false);
            return nullptr;
        }

        const auto rga_started = std::chrono::steady_clock::now();
        ScopedRgaBufferHandle source_handle;
        ScopedRgaBufferHandle target_handle(output->GetData(), output->GetSize());
        const int source_fd = mpp_buffer_get_fd(buffer);
        source_handle.ImportFd(source_fd, mpp_buffer_get_size(buffer));
        IM_STATUS status = IM_STATUS_OUT_OF_MEMORY;
        if (source_handle && target_handle) {
            auto source = wrapbuffer_handle_t(source_handle.Get(), static_cast<int>(width),
                                              static_cast<int>(height), static_cast<int>(horizontal_stride),
                                              static_cast<int>(vertical_stride), source_format);
            auto target =
                wrapbuffer_handle_t(target_handle.Get(), static_cast<int>(width), static_cast<int>(height),
                                    static_cast<int>(width), static_cast<int>(height), RK_FORMAT_YCbCr_420_P);
            if (source_format == RK_FORMAT_YCbCr_420_P) {
                const im_rect source_rect{0, 0, static_cast<int>(width), static_cast<int>(height)};
                const im_rect target_rect = source_rect;
                const im_rect empty_rect{};
                const rga_buffer_t empty_buffer{};
                status =
                    improcess(source, target, empty_buffer, source_rect, target_rect, empty_rect, IM_SYNC);
            } else {
                status = imcvtcolor_t(source, target, source_format, RK_FORMAT_YCbCr_420_P,
                                      IM_COLOR_SPACE_DEFAULT, 1);
            }
        }
        const bool rga_success = RockchipRgaSucceeded(status);
        GetPreviewPipelineMetrics().RecordRgaOperation(rga_success, ElapsedNanoseconds(rga_started));
        GetPreviewPipelineMetrics().RecordMppRgaCopyOut(rga_success);
        if (rga_success) {
            return output;
        }

        static std::atomic_flag fallback_logged = ATOMIC_FLAG_INIT;
        if (!fallback_logged.test_and_set(std::memory_order_relaxed)) {
            LOG_WARN("{} RGA DMA-BUF materialization failed with status {} ({}); using CPU copy-out",
                     decoder_name, status, imStrError_t(status));
        }
        GetPreviewPipelineMetrics().RecordMppCpuCopyOutFallback();
        return CopyMppFrameCpu(decoder_name, frame);
    }

}  // namespace

struct PendingDecodeTiming {
    int64_t pts{0};
};

struct RockchipDecoderState {
    MppCtx context{nullptr};
    MppApi* api{nullptr};
    MppDecCfg config{nullptr};
    MppBufferGroup frame_group{nullptr};
    size_t frame_group_buffer_size{0};
    bool frame_group_reuse_logged{false};
    MppCodingType coding{MPP_VIDEO_CodingUnused};
    bool opened{false};
    std::deque<PendingDecodeTiming> pending;
    std::deque<DecodedVideoFrame> ready_frames;
};

VideoDecoderRockchip::VideoDecoderRockchip(size_t name) : VideoDecoder(name) {}

VideoDecoderRockchip::~VideoDecoderRockchip() {
    VideoDecoderRockchip::Close();
}

VideoDecoderCapability VideoDecoderRockchip::Probe(VideoCodecType type) {
    VideoDecoderCapability capability;
    capability.backend        = "rockchip-mpp-rga";
    capability.implementation = "rockchip-mpp-vpu";

    const auto coding = ToMppCoding(type);
    if (coding == MPP_VIDEO_CodingUnused) {
        const auto cpu            = VideoDecoderCpu::Probe(type);
        capability.available      = cpu.available;
        capability.backend        = cpu.available ? "ffmpeg-software-fallback" : capability.backend;
        capability.implementation = cpu.available ? cpu.implementation : std::string{};
        capability.detail         = cpu.available
                                        ? "Codec is outside the MPP H264/H265 scope; FFmpeg fallback is available"
                                        : "Codec is outside the MPP H264/H265 scope; FFmpeg fallback unavailable";
        return capability;
    }

    const bool device_accessible = access("/dev/mpp_service", R_OK | W_OK) == 0;
    const bool format_supported  = mpp_check_support_format(MPP_CTX_DEC, coding) == MPP_OK;
    if (device_accessible && format_supported) {
        capability.available = true;
        capability.detail =
            "MPP VPU decode and DMA-BUF export are available; selected host frames are materialized "
            "through RGA with an observable CPU fallback";
        return capability;
    }

    const auto cpu = VideoDecoderCpu::Probe(type);
    if (cpu.available) {
        capability.available      = true;
        capability.backend        = "ffmpeg-software-fallback";
        capability.implementation = cpu.implementation;
        capability.detail         = "MPP decoder unavailable; FFmpeg software fallback is available";
        return capability;
    }

    capability.detail = std::string("MPP decoder unavailable (") +
                        (device_accessible ? "codec unsupported" : "device inaccessible") +
                        "); FFmpeg fallback unavailable";
    return capability;
}

bool VideoDecoderRockchip::Open() {
    Close();
    if (OpenMpp()) {
        return true;
    }

    const auto cpu_capability = VideoDecoderCpu::Probe(codec_type_);
    if (!cpu_capability.available) {
        LOG_WARN("{} MPP decoder open failed and FFmpeg fallback is unavailable: {}", idx_name_,
                 cpu_capability.detail);
        return false;
    }

    fallback_ = std::make_unique<VideoDecoderCpu>(0);
    fallback_->SetCodecType(codec_type_, static_cast<int>(width_), static_cast<int>(height_));
    if (!fallback_->Open()) {
        fallback_.reset();
        return false;
    }
    GetPreviewPipelineMetrics().RecordMppDecodeFallback();
    LOG_WARN("{} MPP decoder unavailable; using FFmpeg decoder {}", idx_name_, cpu_capability.implementation);
    return true;
}

bool VideoDecoderRockchip::OpenMpp() {
    const auto coding = ToMppCoding(codec_type_);
    if (coding == MPP_VIDEO_CodingUnused) {
        return false;
    }

    const auto capability = Probe(codec_type_);
    if (!capability.available || capability.implementation != "rockchip-mpp-vpu") {
        LOG_WARN("{} MPP decoder admission failed: {}", idx_name_, capability.detail);
        return false;
    }

    state_         = std::make_unique<RockchipDecoderState>();
    state_->coding = coding;
    auto ret       = mpp_create(&state_->context, &state_->api);
    if (ret != MPP_OK || !state_->context || !state_->api) {
        LOG_WARN("{} MPP decoder context creation failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }
    ret = mpp_init(state_->context, MPP_CTX_DEC, coding);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder initialization failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }

    ret = mpp_dec_cfg_init(&state_->config);
    if (ret != MPP_OK || !state_->config) {
        LOG_WARN("{} MPP decoder config allocation failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }
    ret = state_->api->control(state_->context, MPP_DEC_GET_CFG, state_->config);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder config query failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }
    // VideoDemuxer already emits one Annex-B access unit per VideoPacket.
    ret = mpp_dec_cfg_set_u32(state_->config, "base:split_parse", 0);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder split-parse configuration failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }
    ret = state_->api->control(state_->context, MPP_DEC_SET_CFG, state_->config);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder config commit failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }

    // Some camera and MP4 streams are fully decodable but MPP marks every
    // output frame with recoverable errinfo. Dropping those frames starves the
    // algorithm graph even though discard is clear. Ask MPP to conceal such
    // stream errors and keep discard as the authoritative unusable-frame mark.
    RK_U32 disable_error = 1;
    ret                  = state_->api->control(state_->context, MPP_DEC_SET_DISABLE_ERROR, &disable_error);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder error concealment setup failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }

    // NV12 is the native Rockchip decoder output. It is deinterleaved during the
    // explicit Copy-out boundary, so downstream code still receives I420.
    MppFrameFormat output_format = MPP_FMT_YUV420SP;
    ret = state_->api->control(state_->context, MPP_DEC_SET_OUTPUT_FORMAT, &output_format);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder output-format request failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }

    MppPollType output_timeout = static_cast<MppPollType>(10);
    ret = state_->api->control(state_->context, MPP_SET_OUTPUT_TIMEOUT, &output_timeout);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder timeout configuration failed: {}", idx_name_, ret);
        CleanMpp();
        return false;
    }

    state_->opened = true;
    LOG_INFO("{} MPP VPU decoder opened: codec={} materialize=RGA-I420", idx_name_,
             static_cast<int>(codec_type_));
    return true;
}

bool VideoDecoderRockchip::Close() {
    bool result = true;
    if (fallback_) {
        result = fallback_->Close();
        fallback_.reset();
    }
    CleanMpp();
    return result;
}

bool VideoDecoderRockchip::IsOpened() {
    return (fallback_ && fallback_->IsOpened()) || (state_ && state_->opened);
}

bool VideoDecoderRockchip::ReuseForStreamRestart(VideoCodecType type, int width, int height) {
    if (fallback_ || !state_ || !state_->opened || !state_->context || !state_->api || type != codec_type_ ||
        width <= 0 || height <= 0 || static_cast<size_t>(width) != width_ ||
        static_cast<size_t>(height) != height_) {
        return false;
    }

    // A local loop or RTSP reconnect starts with a validated keyframe. Keep
    // the MPP context and its external frame group alive so synchronous RGA
    // imports never outlive a frame group that is destroyed every few seconds.
    // Codec parameter sets on the new keyframe update the decoder as needed;
    // ConfigureFrameGroup reuses the group when the requested buffer layout is
    // unchanged and reconfigures it when the layout really changes.
    state_->pending.clear();
    state_->ready_frames.clear();
    return true;
}

bool VideoDecoderRockchip::SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) {
    if (fallback_) {
        return fallback_->SendPacket(pkt, len, frame_idx);
    }

    const auto started = std::chrono::steady_clock::now();
    const auto fail    = [&]() {
        GetPreviewPipelineMetrics().RecordMppDecode(false, ElapsedNanoseconds(started));
        return false;
    };
    if (!state_ || !state_->opened || !state_->context || !state_->api || !pkt || len == 0) {
        return fail();
    }

    MppPacket packet = nullptr;
    auto ret         = mpp_packet_init(&packet, const_cast<uint8_t*>(pkt), len);
    if (ret != MPP_OK || !packet) {
        return fail();
    }
    mpp_packet_set_pos(packet, const_cast<uint8_t*>(pkt));
    mpp_packet_set_length(packet, len);
    mpp_packet_set_pts(packet, frame_idx);
    mpp_packet_set_dts(packet, frame_idx);

    bool submitted = false;
    for (int attempt = 0; attempt < kPacketSubmitAttempts; ++attempt) {
        ret = state_->api->decode_put_packet(state_->context, packet);
        if (ret == MPP_OK) {
            submitted = true;
            break;
        }
        if (ret != MPP_ERR_BUFFER_FULL) {
            break;
        }
        // The common VideoDecoder contract calls SendPacket before GetFrame.
        // Under load MPP may therefore have a completed output waiting while
        // its input queue is full. Drain exactly one output, retain it for the
        // subsequent GetFrame call, then retry the same input packet.
        bool made_progress = false;
        auto ready         = ReceiveMppFrame(made_progress);
        if (ready.HasFrame()) {
            state_->ready_frames.push_back(std::move(ready));
        }
        if (!made_progress) {
            std::this_thread::sleep_for(kPacketSubmitWait);
        }
    }
    mpp_packet_deinit(&packet);
    if (!submitted) {
        LOG_WARN("{} MPP decode_put_packet failed: {}", idx_name_, ret);
        return fail();
    }

    state_->pending.push_back({frame_idx});
    while (state_->pending.size() > kMaxPendingTimings) {
        state_->pending.pop_front();
    }
    return true;
}

bool VideoDecoderRockchip::ConfigureFrameGroup(size_t buffer_size) {
    if (!state_ || !state_->context || !state_->api || buffer_size == 0) {
        return false;
    }

    MPP_RET ret                     = MPP_OK;
    const bool reuse_existing_group = state_->frame_group && state_->frame_group_buffer_size == buffer_size;
    if (!state_->frame_group) {
        ret = mpp_buffer_group_get_internal(
            &state_->frame_group,
            static_cast<MppBufferType>(MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE));
    } else if (!reuse_existing_group) {
        ret = mpp_buffer_group_clear(state_->frame_group);
    }
    if (ret != MPP_OK || !state_->frame_group) {
        LOG_WARN("{} MPP decoder frame-group allocation/reset failed: {}", idx_name_, ret);
        return false;
    }
    if (!reuse_existing_group) {
        ret = mpp_buffer_group_limit_config(state_->frame_group, buffer_size, kDecoderBufferCount);
        if (ret != MPP_OK) {
            LOG_WARN("{} MPP decoder frame-group limit failed: {}", idx_name_, ret);
            return false;
        }
        ret = state_->api->control(state_->context, MPP_DEC_SET_EXT_BUF_GROUP, state_->frame_group);
        if (ret != MPP_OK) {
            LOG_WARN("{} MPP decoder external frame-group setup failed: {}", idx_name_, ret);
            return false;
        }
        state_->frame_group_buffer_size = buffer_size;
    }
    ret = state_->api->control(state_->context, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
    if (ret != MPP_OK) {
        LOG_WARN("{} MPP decoder info-change acknowledgement failed: {}", idx_name_, ret);
        if (!reuse_existing_group) {
            state_->frame_group_buffer_size = 0;
        }
        return false;
    }
    return true;
}

DecodedVideoFrame VideoDecoderRockchip::ReceiveMppFrame(bool& made_progress) {
    made_progress = false;
    if (!state_ || !state_->opened || !state_->context || !state_->api) {
        return {};
    }

    const auto decode_started = std::chrono::steady_clock::now();
    MppFrame frame            = nullptr;
    const auto ret            = state_->api->decode_get_frame(state_->context, &frame);
    if (ret == MPP_ERR_TIMEOUT || (ret == MPP_OK && !frame)) {
        return {};
    }
    if (ret != MPP_OK) {
        made_progress = true;
        GetPreviewPipelineMetrics().RecordMppDecode(false, ElapsedNanoseconds(decode_started));
        LOG_WARN("{} MPP decode_get_frame failed: {}", idx_name_, ret);
        return {};
    }

    if (mpp_frame_get_info_change(frame)) {
        made_progress           = true;
        const auto buffer_size  = mpp_frame_get_buf_size(frame);
        const bool reused_group = state_->frame_group && state_->frame_group_buffer_size == buffer_size;
        const auto configured   = ConfigureFrameGroup(buffer_size);
        if (!reused_group || !configured) {
            LOG_INFO("{} MPP decoder info change: {}x{} stride={}x{} buffer={} configured={} reused={}",
                     idx_name_, mpp_frame_get_width(frame), mpp_frame_get_height(frame),
                     mpp_frame_get_hor_stride(frame), mpp_frame_get_ver_stride(frame), buffer_size,
                     configured, reused_group);
        } else if (!state_->frame_group_reuse_logged) {
            state_->frame_group_reuse_logged = true;
            LOG_INFO("{} MPP decoder reuses its existing {}-byte frame group for repeated stream headers",
                     idx_name_, buffer_size);
        }
        mpp_frame_deinit(&frame);
        if (!configured) {
            GetPreviewPipelineMetrics().RecordMppDecode(false, ElapsedNanoseconds(decode_started));
        }
        return {};
    }

    const auto pts = mpp_frame_get_pts(frame);
    made_progress  = true;
    auto timing    = state_->pending.end();
    if (pts >= 0) {
        timing = std::find_if(state_->pending.begin(), state_->pending.end(),
                              [pts](const auto& item) { return item.pts == pts; });
    }
    if (timing == state_->pending.end() && !state_->pending.empty()) {
        timing = state_->pending.begin();
    }
    const bool frame_valid = mpp_frame_get_errinfo(frame) == 0 && mpp_frame_get_discard(frame) == 0;
    if (!frame_valid) {
        LOG_WARN("{} MPP decoder discarded frame pts={} err=0x{:x} discard=0x{:x}", idx_name_, pts,
                 mpp_frame_get_errinfo(frame), mpp_frame_get_discard(frame));
        mpp_frame_deinit(&frame);
        if (timing != state_->pending.end()) {
            state_->pending.erase(timing);
        }
        GetPreviewPipelineMetrics().RecordMppDecode(false, ElapsedNanoseconds(decode_started));
        return {};
    }

    int64_t resolved_pts = pts;
    if (resolved_pts < 0 && timing != state_->pending.end()) {
        resolved_pts = timing->pts;
    }
    if (timing != state_->pending.end()) {
        state_->pending.erase(timing);
    }
    const size_t frame_width  = mpp_frame_get_width(frame);
    const size_t frame_height = mpp_frame_get_height(frame);
    width_                    = frame_width;
    height_                   = frame_height;
    auto holder               = std::make_shared<MppFrameHolder>(frame);
    frame                     = nullptr;
    const auto decoder_name   = idx_name_;

    // This counter measures a valid MPP output becoming available. Host
    // allocation and copying are measured separately by RecordMppCopyOut.
    GetPreviewPipelineMetrics().RecordMppDecode(true, ElapsedNanoseconds(decode_started));
    return DecodedVideoFrame(
        static_cast<uint64_t>(std::max<int64_t>(0, resolved_pts)), frame_width, frame_height,
        PixelFormat::PIXEL_I420,
        [holder, decoder_name, resolved_pts]() {
            const auto copy_started = std::chrono::steady_clock::now();
            auto output             = MaterializeMppFrame(decoder_name, holder->frame);
            if (output) {
                output->SetFrameIndex(static_cast<uint64_t>(std::max<int64_t>(0, resolved_pts)));
            }
            GetPreviewPipelineMetrics().RecordMppCopyOut(output != nullptr, ElapsedNanoseconds(copy_started));
            return output;
        },
        []() { GetPreviewPipelineMetrics().RecordMppEarlyDrop(); },
        [holder, decoder_name]() { return ExportMppBuffer(decoder_name, holder->frame); });
}

VideoFramePtr VideoDecoderRockchip::GetFrame() {
    auto frame = GetDecodedFrame();
    return frame.Materialize();
}

DecodedVideoFrame VideoDecoderRockchip::GetDecodedFrame() {
    if (fallback_) {
        return DecodedVideoFrame(fallback_->GetFrame());
    }
    if (!state_ || !state_->opened) {
        return {};
    }
    if (!state_->ready_frames.empty()) {
        auto output = std::move(state_->ready_frames.front());
        state_->ready_frames.pop_front();
        return output;
    }
    bool made_progress = false;
    return ReceiveMppFrame(made_progress);
}

void VideoDecoderRockchip::CleanMpp() {
    if (!state_) {
        return;
    }
    state_->opened = false;
    state_->pending.clear();
    state_->ready_frames.clear();
    if (state_->context && state_->api) {
        state_->api->reset(state_->context);
    }
    if (state_->context) {
        mpp_destroy(state_->context);
        state_->context = nullptr;
        state_->api     = nullptr;
    }
    if (state_->frame_group) {
        mpp_buffer_group_put(state_->frame_group);
        state_->frame_group = nullptr;
    }
    if (state_->config) {
        mpp_dec_cfg_deinit(state_->config);
        state_->config = nullptr;
    }
    state_.reset();
}

}  // namespace cosmo::media
