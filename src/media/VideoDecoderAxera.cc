#include "media/VideoDecoderAxera.h"
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
#include "nn/device/axera/facelandmark/JpegAdmission.h"
#endif

#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "ax_buffer_tool.h"
#include "ax_sys_api.h"
#include "ax_vdec_api.h"
#include "media/AxeraVideoFrameCopy.h"
#include "media/H264AnnexBColorParser.h"
#include "media/NativeFrameLeasePool.h"
#include "media/VideoColorProvenance.h"
#include "media/VideoPacketColorWitness.h"
#include "nn/device/axera/axera_sys.h"
#include "util/Log.h"

namespace cosmo::media {
namespace {

    constexpr AX_VDEC_CHN kOutputChannel   = 0;
    constexpr AX_U32 kMaxGroups            = 64;
    constexpr AX_U32 kOutputDepth          = 5;
    constexpr AX_U32 kH264FrameBuffers     = 32;
    constexpr AX_U32 kH265FrameBuffers     = 8;
    constexpr size_t kMaxPendingInputs     = 128;
    constexpr size_t kMaxPendingInputBytes = 64 * 1024 * 1024;

    struct VdecModuleState {
        std::mutex mutex;
        size_t references{0};
    };

    VdecModuleState& ModuleState() {
        // Leases may be destroyed during process teardown; do not depend on static
        // destruction order relative to decoder or shared AX SYS objects.
        static auto* state = new VdecModuleState;
        return *state;
    }

    class VdecModuleLease {
    public:
        ~VdecModuleLease() {
            Reset();
        }
        bool Acquire() {
            auto& state = ModuleState();
            std::lock_guard<std::mutex> lock(state.mutex);
            if (held_)
                return true;
            if (state.references == 0) {
                AX_VDEC_MOD_ATTR_T attr{};
                attr.u32MaxGroupCount = kMaxGroups;
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
                attr.enDecModule = AX_ENABLE_BOTH_VDEC_JDEC;
#else
                attr.enDecModule = AX_ENABLE_ONLY_VDEC;
#endif
                const AX_S32 result = AX_VDEC_Init(&attr);
                if (result != 0) {
                    LOG_ERRO("AX VDEC init failed: {}", result);
                    return false;
                }
            }
            ++state.references;
            held_ = true;
            return true;
        }
        void Reset() {
            auto& state = ModuleState();
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!held_)
                return;
            held_ = false;
            if (--state.references == 0)
                AX_VDEC_Deinit();
        }

    private:
        bool held_{false};
    };

    class MappedPlane {
    public:
        MappedPlane(AX_U64 address, AX_U32 bytes) : bytes_(bytes) {
            if (address && bytes)
                ptr_ = AX_SYS_Mmap(address, bytes);
        }
        ~MappedPlane() {
            if (ptr_)
                AX_SYS_Munmap(ptr_, bytes_);
        }
        const uint8_t* Get() const {
            return static_cast<const uint8_t*>(ptr_);
        }

    private:
        AX_VOID* ptr_{nullptr};
        AX_U32 bytes_{0};
    };

    struct StreamStorage {
        AX_U64 physical{0};
        AX_VOID* virtual_address{nullptr};
        bool cached{false};
        size_t bytes{0};
        size_t capacity{0};
        uint64_t original_frame_index{0};
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
        bool jfif_admitted{false};
#endif
        int64_t source_timestamp{0};
        int64_t source_arrival_timestamp{0}, presentation_steady_ms{0};
        ~StreamStorage() {
            if (virtual_address)
                AX_SYS_MemFree(physical, virtual_address);
        }
    };

    AX_PAYLOAD_TYPE_E Payload(VideoCodecType type) {
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
        if (type == VideoCodecType::kMjpeg)
            return PT_MJPEG;
#endif
        if (type == VideoCodecType::kH264)
            return PT_H264;
        if (type == VideoCodecType::kH265)
            return PT_H265;
        return PT_BUTT;
    }

    AX_U32 Align(AX_U32 value, AX_U32 alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

}  // namespace

struct AxeraDecoderState {
    std::mutex mutex;
    std::array<unsigned, 16> diag_counts{};
    nn::AxeraSysLease sys;
    VdecModuleLease module;
    AX_VDEC_GRP group{-1};
    bool channel_enabled{false};
    bool receiving{false};
    bool fault{false};
    bool held_frame_valid{false};
    using NativePool = native_frame_candidate::FrameLeasePool<AX_VIDEO_FRAME_INFO_T>;
    std::unique_ptr<NativePool> native_pool;
    size_t native_capacity{1};
    bool transient_selection{false};
    bool trace = [] {
        const char* p = std::getenv("COSMO_AXERA_VDEC_TRANSIENT_TRACE");
        return p && std::strcmp(p, "1") == 0;
    }();
    bool trace_selected_pending{false}, trace_last_fifth{false};
    std::chrono::steady_clock::time_point trace_selection_started{};
    uint64_t trace_selection_complete{0}, trace_selection_wait_ns{0}, trace_selection_max_ns{0};
    uint64_t trace_send_calls{0}, trace_send_ns{0}, trace_get_calls{0}, trace_get_ns{0}, trace_get_empty{0};
    uint64_t trace_copy_ns{0}, trace_witness_ns{0}, trace_parser_ns{0}, trace_flush_ns{0};
    uint64_t trace_fifth_get{0}, trace_selected{0}, trace_select_wait_polls{0}, trace_input_wait_polls{0};
    bool native_out_of_order{false};
    bool native_only{false};
    bool native_closing{false};
    bool color_policy_required{false};
    uint64_t color_generation{0};
    bool awaiting_first_idr{true};
    bool color_resolution_logged{false};
    H264AnnexBColorParser color_parser;
    VideoColorProvenance color_provenance;
    std::thread::id native_owner_thread{};
    AX_VIDEO_FRAME_INFO_T held_frame{};
    AX_U32 stream_capacity{0};
    uint64_t next_stream_pts{1};
    size_t pending_bytes{0};
    std::map<uint64_t, std::unique_ptr<StreamStorage>> pending_inputs;
    // The historical decoder reused completed compressed-stream buffers. A
    // slot enters this bounded pool only after its exact PTS frame is released.
    std::array<std::unique_ptr<StreamStorage>, 8> free_streams{};
    size_t free_stream_count{0};
    size_t free_stream_bytes{0};
    bool stream_pool_enabled{false};
    static constexpr size_t kFreeStreamBytes = 8 * 1024 * 1024;

    void CompleteInput(uint64_t pts) {
        const auto found = pending_inputs.find(pts);
        if (found == pending_inputs.end())
            return;
        pending_bytes -= found->second->bytes;
        auto input = std::move(found->second);
        pending_inputs.erase(found);
        if (stream_pool_enabled && input && free_stream_count < free_streams.size() &&
            input->capacity <= kFreeStreamBytes - free_stream_bytes) {
            free_stream_bytes += input->capacity;
            free_streams[free_stream_count++] = std::move(input);
        }
    }

    bool ReleaseHeldFrame() noexcept {
        if (!held_frame_valid)
            return true;
        if (AX_VDEC_ReleaseChnFrame(group, kOutputChannel, &held_frame) != 0) {
            fault = true;
            return false;
        }
        const auto pts   = held_frame.stVFrame.u64PTS;
        held_frame_valid = false;
        CompleteInput(pts);
        return true;
    }

    size_t NativeOutstanding() const noexcept {
        return native_pool ? native_pool->GetStatus().outstanding : 0;
    }

    bool ReleaseNativeFrame(AX_VIDEO_FRAME_INFO_T& frame) noexcept {
        try {
            if (AX_VDEC_ReleaseChnFrame(group, kOutputChannel, &frame) != 0) {
                fault = true;
                return false;
            }
            CompleteInput(frame.stVFrame.u64PTS);
            return true;
        } catch (...) {
            fault = true;
            return false;
        }
    }

    // Consumer destruction marks only its own slot. The decoder owner is the
    // only thread issuing releases, including out-of-order returned slots.
    bool DrainReturnedNativeFrame() noexcept {
        if (!native_pool)
            return true;
        const auto release = [this](AX_VIDEO_FRAME_INFO_T& f) noexcept { return ReleaseNativeFrame(f); };
        const bool ok =
            native_out_of_order ? native_pool->DrainReturned(release) : native_pool->DrainOrdered(release);
        if (!ok)
            fault = true;
        return ok;
    }

    bool ResetWithoutNative() {
        if (!ReleaseHeldFrame())
            return false;
        if (receiving) {
            if (AX_VDEC_StopRecvStream(group) != 0) {
                fault = true;
                return false;
            }
            receiving = false;
        }
        if (channel_enabled) {
            if (AX_VDEC_DisableChn(group, kOutputChannel) != 0) {
                fault = true;
                return false;
            }
            channel_enabled = false;
        }
        if (group >= 0) {
            const auto result = AX_VDEC_DestroyGrp(group);
            if (result != 0) {
                LOG_ERRO("AX VDEC group destroy failed: {}; preserving inputs", result);
                return false;
            }
        }
        group = -1;
        pending_inputs.clear();
        for (auto& input : free_streams)
            input.reset();
        free_stream_count   = 0;
        free_stream_bytes   = 0;
        stream_pool_enabled = false;
        color_provenance.Invalidate();
        pending_bytes       = 0;
        stream_capacity     = 0;
        fault               = false;
        native_owner_thread = {};
        native_closing      = false;
        native_capacity     = 1;
        transient_selection = false;
        native_out_of_order = false;
        module.Reset();
        sys.Reset();
        return true;
    }

    bool Reset() {
        if (!native_pool)
            return ResetWithoutNative();
        native_closing = true;
        // All pool operations are serialized by the decoder mutex. A
        // non-owner may close only an already empty pool; never issue frame
        // release from that thread.
        bool ok = false;
        if (native_owner_thread == std::this_thread::get_id()) {
            if (!native_pool->BeginStop())
                return false;
            ok = native_pool->FinishStop(
                [this](AX_VIDEO_FRAME_INFO_T& f) noexcept { return ReleaseNativeFrame(f); },
                [this]() noexcept {
                    try {
                        return ResetWithoutNative();
                    } catch (...) {
                        fault = true;
                        return false;
                    }
                });
        } else {
            ok = native_pool->FinishIdleStop([this]() noexcept {
                try {
                    return ResetWithoutNative();
                } catch (...) {
                    fault = true;
                    return false;
                }
            });
        }
        if (!ok)
            return false;
        native_pool.reset();
        return true;
    }
};

namespace {
    class FrameLease {
    public:
        explicit FrameLease(AxeraDecoderState& state) noexcept : state_(state) {}
        ~FrameLease() noexcept {
            if (active_)
                state_.ReleaseHeldFrame();
        }
        bool Release() noexcept {
            active_ = false;
            return state_.ReleaseHeldFrame();
        }
        void Disarm() noexcept {
            active_ = false;
        }

    private:
        AxeraDecoderState& state_;
        bool active_{true};
    };
}  // namespace

VideoDecoderAxera::VideoDecoderAxera(size_t name)
    : VideoDecoder(name), state_(new AxeraDecoderState, [](AxeraDecoderState* state) {
          // Only a successful Close can release SDK leases. If the decoder
          // dies before the consumer, preserve the state rather than making
          // SDK calls from an unverified thread or freeing in-flight memory.
          if (state->group < 0 && !state->held_frame_valid)
              delete state;
      }) {}

VideoDecoderAxera::~VideoDecoderAxera() {
    if (!Close()) {
        // If teardown fails, the driver may still access pending inputs.
        // Retain the state and its SDK leases until process exit.
        state_.reset();
    }
}

VideoDecoderCapability VideoDecoderAxera::Probe(VideoCodecType type) {
    VideoDecoderCapability result;
    result.backend        = "axera-vdec";
    result.implementation = "ax650-vdec";
    if (Payload(type) == PT_BUTT) {
        result.detail = "unsupported codec";
    } else {
        result.available = access("/dev/ax_vdec", R_OK | W_OK) == 0;
        result.detail = result.available ? "AX VDEC device node accessible; actual initialization not probed"
                                         : "AX VDEC device node missing or inaccessible";
    }
    return result;
}

bool VideoDecoderAxera::Open() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->native_closing)
        return false;
    if (state_->receiving && !state_->fault)
        return true;
    if (!state_->Reset())
        return false;
    const auto payload = Payload(codec_type_);
    if (payload == PT_BUTT || width_ < AX_VDEC_MIN_WIDTH || height_ < AX_VDEC_MIN_HEIGHT ||
        width_ > AX_VDEC_MAX_WIDTH || height_ > AX_VDEC_MAX_HEIGHT) {
        LOG_ERRO("{} AX VDEC unsupported codec or dimensions {}x{}", idx_name_, width_, height_);
        return false;
    }
    const auto fail = [this](const char* step, AX_S32 code) {
        LOG_ERRO("{} AX VDEC {} failed: {}", idx_name_, step, code);
        state_->Reset();
        return false;
    };
    const auto sys_result = state_->sys.Acquire();
    if (sys_result != 0)
        return fail("AX_SYS_Init", sys_result);
    if (!state_->module.Acquire())
        return fail("module init", -1);

    const auto aligned_width  = Align(static_cast<AX_U32>(width_), 16);
    const auto aligned_height = Align(static_cast<AX_U32>(height_), 16);
    AX_VDEC_GRP_ATTR_T group{};
    group.enCodecType       = payload;
    group.enInputMode       = AX_VDEC_INPUT_MODE_FRAME;
    group.u32MaxPicWidth    = aligned_width;
    group.u32MaxPicHeight   = aligned_height;
    group.u32StreamBufSize  = std::max<AX_U32>(aligned_width * aligned_height * 3 / 2, 1024 * 1024);
    group.bSdkAutoFramePool = AX_TRUE;
    AX_S32 result           = AX_VDEC_CreateGrpEx(&state_->group, &group);
    if (result != 0) {
        state_->group = -1;
        return fail("group creation", result);
    }
    AX_VDEC_CHN_ATTR_T channel{};
    channel.u32PicWidth                   = aligned_width;
    channel.u32PicHeight                  = aligned_height;
    channel.u32FrameStride                = Align(aligned_width, 256);
    channel.u32OutputFifoDepth            = kOutputDepth;
    channel.u32FrameBufCnt                = payload == PT_H264 ? kH264FrameBuffers : kH265FrameBuffers;
    channel.enOutputMode                  = AX_VDEC_OUTPUT_ORIGINAL;
    channel.enImgFormat                   = AX_FORMAT_YUV420_SEMIPLANAR;
    channel.stCompressInfo.enCompressMode = AX_COMPRESS_MODE_NONE;
    channel.u32FrameBufSize = AX_VDEC_GetPicBufferSize(channel.u32FrameStride, channel.u32PicHeight,
                                                       channel.enImgFormat, &channel.stCompressInfo, payload);
    result                  = AX_VDEC_SetChnAttr(state_->group, kOutputChannel, &channel);
    if (result != 0)
        return fail("channel configuration", result);
    result = AX_VDEC_EnableChn(state_->group, kOutputChannel);
    if (result != 0)
        return fail("channel enable", result);
    state_->channel_enabled = true;
    state_->stream_capacity = group.u32StreamBufSize;
    AX_VDEC_RECV_PIC_PARAM_T receive{};
    receive.s32RecvPicNum = -1;
    result                = AX_VDEC_StartRecvStream(state_->group, &receive);
    if (result != 0)
        return fail("start receive", result);
    state_->receiving = true;
    LOG_INFO("{} AX VDEC group {} opened, {}x{}; output mode {}", idx_name_, state_->group, width_, height_,
             state_->native_only ? "native-only" : "host I420");
    return true;
}

bool VideoDecoderAxera::Close() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->Reset();
}

bool VideoDecoderAxera::IsOpened() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->receiving && !state_->fault && !state_->native_closing;
}

bool VideoDecoderAxera::SendPacket(const uint8_t* packet, size_t size, int64_t frame_index) {
    return SendPacketImpl(packet, size, frame_index, nullptr);
}

bool VideoDecoderAxera::SendNativePacket(const VideoPacket& packet) {
    return SendPacketImpl(packet.data.data(), packet.data.size(), packet.index, &packet);
}

DecodedVideoFrame VideoDecoderAxera::DecodeNativePacket(const VideoPacket& packet, bool& result) {
    const bool sent = SendNativePacket(packet);
    ++send_pkt_cnt_;
    result = sent;
    if (!sent)
        return {};
    auto frame = GetDecodedFrame();
    if (frame.HasFrame())
        ++recv_frame_cnt_;
    return frame;
}

bool VideoDecoderAxera::SendPacketImpl(const uint8_t* packet, size_t size, int64_t frame_index,
                                       const VideoPacket* color_packet) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto trace = [this](size_t slot, const char* reason) {
        if (state_->diag_counts[slot] < 3 && ++state_->diag_counts[slot] > 0)
            LOG_WARN("{} AX VDEC diag send reject: {}", idx_name_, reason);
    };
    if (state_->native_closing) {
        trace(0, "native_closing");
        return false;
    }
    if (!state_->DrainReturnedNativeFrame()) {
        trace(0, "drain_native_lease");
        return false;
    }
    if (color_packet && (state_->NativeOutstanding() >= state_->native_capacity)) {
        trace(1, "native_lease_active");
        return false;
    }
    if (!state_->receiving || state_->fault || !packet || !size || size > state_->stream_capacity ||
        frame_index < 0) {
        if (state_->diag_counts[2] < 3 && ++state_->diag_counts[2] > 0)
            LOG_WARN(
                "{} AX VDEC diag invalid packet: receiving={} fault={} present={} size={} capacity={} "
                "index_negative={}",
                idx_name_, state_->receiving, state_->fault, packet != nullptr, size, state_->stream_capacity,
                frame_index < 0);
        return false;
    }
    VideoColorEvidence evidence;
    uint64_t color_generation = 0;
    if (color_packet) {
        state_->color_policy_required = true;
        if (!state_->native_only || color_packet->stream_idx <= 0) {
            trace(3, "native_mode_or_stream_generation");
            state_->color_provenance.Invalidate();
            return false;
        }
        color_generation = static_cast<uint64_t>(color_packet->stream_idx);
        if (color_generation != state_->color_generation) {
            if (!state_->pending_inputs.empty() || state_->held_frame_valid ||
                state_->NativeOutstanding() != 0 || !state_->color_parser.BeginGeneration(color_generation) ||
                !state_->color_provenance.BeginGeneration(color_generation)) {
                trace(4, "generation_transition");
                state_->color_provenance.Invalidate();
                return false;
            }
            state_->color_generation        = color_generation;
            state_->awaiting_first_idr      = true;
            state_->color_resolution_logged = false;
        }
        if (state_->color_provenance.Faulted() || color_packet->codec_type != VideoCodecType::kH264 ||
            !color_packet->color_packet_pts_correlated || color_packet->color_parameter_change) {
            if (state_->diag_counts[5] < 3 && ++state_->diag_counts[5] > 0)
                LOG_WARN(
                    "{} AX VDEC diag color precondition: fault={} h264={} pts_correlated={} "
                    "parameter_change={}",
                    idx_name_, state_->color_provenance.Faulted(),
                    color_packet->codec_type == VideoCodecType::kH264,
                    color_packet->color_packet_pts_correlated, color_packet->color_parameter_change);
            state_->color_provenance.Invalidate();
            return false;
        }
    } else if (state_->color_policy_required) {
        trace(6, "color_packet_missing");
        state_->color_provenance.Invalidate();
        return false;
    }
    if (state_->pending_inputs.size() >= kMaxPendingInputs ||
        size > kMaxPendingInputBytes - state_->pending_bytes ||
        state_->next_stream_pts == std::numeric_limits<uint64_t>::max()) {
        LOG_WARN("{} AX VDEC pending input limit reached", idx_name_);
        state_->fault = true;
        return false;
    }
    const char* stream_pool_opt = std::getenv("COSMO_AXERA_BENCH_VDEC_STREAM_POOL");
    state_->stream_pool_enabled =
        stream_pool_opt && std::strcmp(stream_pool_opt, "1") == 0 && state_->native_only;
    const char* cached_stream_opt = std::getenv("COSMO_AXERA_BENCH_VDEC_CACHED_STREAM");
    const bool cached_stream      = state_->stream_pool_enabled && color_packet && cached_stream_opt &&
                               std::strcmp(cached_stream_opt, "1") == 0;
    std::unique_ptr<StreamStorage> input;
    if (state_->stream_pool_enabled) {
        size_t best = state_->free_stream_count;
        for (size_t i = 0; i < state_->free_stream_count; ++i)
            if (state_->free_streams[i]->cached == cached_stream &&
                state_->free_streams[i]->capacity >= size &&
                (best == state_->free_stream_count ||
                 state_->free_streams[i]->capacity < state_->free_streams[best]->capacity))
                best = i;
        if (best < state_->free_stream_count) {
            input = std::move(state_->free_streams[best]);
            state_->free_stream_bytes -= input->capacity;
            --state_->free_stream_count;
            if (best != state_->free_stream_count)
                state_->free_streams[best] = std::move(state_->free_streams[state_->free_stream_count]);
        }
    }
    if (!input) {
        input = std::make_unique<StreamStorage>();
        const auto allocation =
            cached_stream
                ? AX_SYS_MemAllocCached(&input->physical, &input->virtual_address, static_cast<AX_U32>(size),
                                        0x1000, reinterpret_cast<const AX_S8*>("cosmo-vdec-cached"))
                : AX_SYS_MemAlloc(&input->physical, &input->virtual_address, static_cast<AX_U32>(size),
                                  0x1000, reinterpret_cast<const AX_S8*>("cosmo-vdec"));
        if (allocation != 0 || !input->virtual_address) {
            LOG_WARN("{} AX VDEC input allocation failed: {}", idx_name_, allocation);
            return false;
        }
        input->capacity = size;
        input->cached   = cached_stream;
    }
    input->bytes                    = size;
    input->original_frame_index     = static_cast<uint64_t>(frame_index);
    input->source_timestamp         = 0;
    input->source_arrival_timestamp = 0;
    input->presentation_steady_ms   = 0;
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
    input->jfif_admitted = false;
#endif
    const auto trace_copy_start =
        state_->trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    std::memcpy(input->virtual_address, packet, size);
    if (state_->trace)
        state_->trace_copy_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - trace_copy_start)
                                     .count();
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
    if (codec_type_ == VideoCodecType::kMjpeg) {
        if (!state_->native_only || color_packet ||
            !face_landmark_candidate::AdmitJpeg601Full(
                static_cast<const std::uint8_t*>(input->virtual_address), size))
            return false;
        input->jfif_admitted = true;
    }
#endif
    if (color_packet) {
        bool witness_valid = color_packet->presentation_time_valid && color_packet->color_witness;
        if (witness_valid) {
            const auto trace_witness_start =
                state_->trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            witness_valid = color_packet->color_witness->Matches(
                *color_packet, static_cast<const uint8_t*>(input->virtual_address), size);
            if (state_->trace)
                state_->trace_witness_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                std::chrono::steady_clock::now() - trace_witness_start)
                                                .count();
        }
        if (!witness_valid) {
            if (state_->diag_counts[7] < 3 && ++state_->diag_counts[7] > 0)
                LOG_WARN("{} AX VDEC diag witness: presentation_valid={} witness_present={}", idx_name_,
                         color_packet->presentation_time_valid, color_packet->color_witness != nullptr);
            state_->color_provenance.Invalidate();
            return false;
        }
        input->source_arrival_timestamp = color_packet->timestamp;
        input->source_timestamp         = color_packet->presentation_timestamp_ms;
        input->presentation_steady_ms   = color_packet->presentation_steady_ms;
        // Verify the exact retained bytes passed to VDEC, not a separate
        // potentially mutable view of the original compressed packet.
        const auto trace_parser_start =
            state_->trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const bool parser_valid = state_->color_parser.ParseAccessUnit(
            color_generation, static_cast<const uint8_t*>(input->virtual_address), size, evidence);
        if (state_->trace)
            state_->trace_parser_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now() - trace_parser_start)
                                           .count();
        if (!parser_valid) {
            trace(8, "h264_color_parser");
            state_->color_provenance.Invalidate();
            return false;
        }
        if (state_->awaiting_first_idr && !evidence.idr) {
            trace(9, "first_access_unit_not_idr");
            state_->color_provenance.Invalidate();
            return false;
        }
        state_->awaiting_first_idr = false;
        evidence.container_present = color_packet->color_container_present;
        evidence.container_space   = color_packet->container_color_space;
        evidence.container_range   = color_packet->container_color_range;
    }
    const auto trace_flush_start =
        state_->trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto flush_status =
        input->cached ? AX_SYS_MflushCache(input->physical, input->virtual_address, static_cast<AX_U32>(size))
                      : 0;
    if (state_->trace && input->cached)
        state_->trace_flush_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - trace_flush_start)
                                      .count();
    if (flush_status != 0) {
        // Nothing was submitted to VDEC. Retain the exact allocation under
        // the pending owner and fault the generation after uncertain flush.
        state_->pending_bytes += size;
        state_->pending_inputs.emplace(state_->next_stream_pts, std::move(input));
        state_->fault = true;
        if (color_packet)
            state_->color_provenance.Invalidate();
        LOG_WARN("{} AX VDEC cached input flush failed; retaining input", idx_name_);
        return false;
    }
    const auto pts = state_->next_stream_pts;
    if (color_packet && !state_->color_provenance.Register(color_generation, pts, frame_index, true,
                                                           color_packet->pts, evidence)) {
        trace(10, "color_registration");
        return false;
    }
    if (color_packet && !state_->color_resolution_logged) {
        const auto origin = state_->color_provenance.LastMatrixOrigin();
        const char* label = origin == ColorMatrixOrigin::Bitstream       ? "bitstream"
                            : origin == ColorMatrixOrigin::MediaMetadata ? "media_metadata"
                                                                         : "application_default_bt709";
        LOG_INFO("{} AX color matrix source={} (application_default is an assumption)", idx_name_, label);
        state_->color_resolution_logged = true;
    }
    ++state_->next_stream_pts;
    AX_VDEC_STREAM_T stream{};
    stream.u64PTS           = pts;
    stream.bEndOfFrame      = AX_TRUE;
    stream.u32StreamPackLen = static_cast<AX_U32>(size);
    stream.pu8Addr          = static_cast<AX_U8*>(input->virtual_address);
    state_->pending_bytes += size;
    state_->pending_inputs.emplace(pts, std::move(input));
    const auto trace_send_start =
        state_->trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const AX_S32 result = AX_VDEC_SendStream(state_->group, &stream, 100);
    if (state_->trace) {
        ++state_->trace_send_calls;
        state_->trace_send_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - trace_send_start)
                                     .count();
        if (state_->trace_send_calls % 64 == 0)
            LOG_INFO(
                "{} AX_VDEC_TRANSIENT_PROFILE enabled={} capacity={} send_calls={} send_ns={} get_calls={} "
                "get_ns={} empty={} fifth={} selected={} selection_complete={} selection_wait_ns={} "
                "selection_max_ns={} select_wait_polls={} input_wait_polls={} copy_ns={} witness_ns={} "
                "parser_ns={} flush_ns={} cached_stream={}",
                idx_name_, state_->transient_selection, state_->native_capacity, state_->trace_send_calls,
                state_->trace_send_ns, state_->trace_get_calls, state_->trace_get_ns, state_->trace_get_empty,
                state_->trace_fifth_get, state_->trace_selected, state_->trace_selection_complete,
                state_->trace_selection_wait_ns, state_->trace_selection_max_ns,
                state_->trace_select_wait_polls, state_->trace_input_wait_polls, state_->trace_copy_ns,
                state_->trace_witness_ns, state_->trace_parser_ns, state_->trace_flush_ns, cached_stream);
    }
    if (result != 0) {
        if (color_packet)
            state_->color_provenance.Invalidate();
        // A timeout could race with submission. Without a documented transfer
        // contract, preserve this input until a matching output or teardown.
        LOG_WARN("{} AX VDEC send failed: {}; retaining input", idx_name_, result);
    }
    return result == 0;
}

namespace {
    bool FetchHostFrame(AxeraDecoderState& state, const std::string& name) {
#ifdef COSMO_AXERA_ELECTRIC_ROUTE_CANDIDATE
        // Test and deployment can opt into a process-wide no-host-pixels policy.
        // Refuse before retrieving, mapping or copying any decoded frame.
        const char* no_host_pixels = std::getenv("COSMO_AXERA_ENFORCE_NO_HOST_PIXELS");
        if (no_host_pixels && std::strcmp(no_host_pixels, "1") == 0)
            throw std::runtime_error("AX host pixel materialization blocked by strict policy");
#endif
        if (state.native_only)
            throw std::runtime_error("AX VDEC native-only decoder cannot materialize host pixels");
        if (!state.DrainReturnedNativeFrame() || state.NativeOutstanding() != 0)
            throw std::runtime_error("AX VDEC native frame lease is not released");
        if (state.fault)
            throw std::runtime_error("AX VDEC is faulted; close and reopen");
        if (!state.receiving)
            return false;
        if (state.held_frame_valid)
            throw std::runtime_error("AX VDEC host frame is still held");
        const auto result = AX_VDEC_GetChnFrame(state.group, kOutputChannel, &state.held_frame, 50);
        if (result == AX_ERR_VDEC_BUF_EMPTY || result == AX_ERR_VDEC_QUEUE_EMPTY ||
            result == AX_ERR_VDEC_TIMED_OUT) {
            if (state.diag_counts[14] < 3 && ++state.diag_counts[14] > 0)
                LOG_WARN("{} AX VDEC diag host GetChnFrame no output: rc={} group={}", name, result,
                         state.group);
            return false;
        }
        if (result != 0) {
            state.fault = true;
            if (state.diag_counts[11] < 3 && ++state.diag_counts[11] > 0)
                LOG_WARN("{} AX VDEC diag host GetChnFrame failed: rc={} group={}", name, result,
                         state.group);
            throw std::runtime_error("AX VDEC frame retrieval failed");
        }
        state.held_frame_valid = true;
        return true;
    }

    struct HostFrameLayout {
        uint64_t index;
        size_t width;
        size_t height;
        size_t y_stride;
        size_t uv_stride;
        AX_U32 y_bytes;
        AX_U32 uv_bytes;
        size_t y_offset;
        size_t uv_offset;
        bool vu;
    };

    HostFrameLayout DescribeHostFrame(const AxeraDecoderState& state, const std::string& name) {
        const auto& source = state.held_frame.stVFrame;
        const auto input   = state.pending_inputs.find(source.u64PTS);
        const auto original_index =
            input == state.pending_inputs.end() ? 0 : input->second->original_frame_index;
        if (input == state.pending_inputs.end()) {
            LOG_WARN("{} AX VDEC returned unknown input PTS {}", name, source.u64PTS);
            throw std::runtime_error("AX VDEC returned unknown input PTS");
        }
        const bool vu = source.enImgFormat == AX_FORMAT_YUV420_SEMIPLANAR_VU;
        if (source.enImgFormat != AX_FORMAT_YUV420_SEMIPLANAR && !vu) {
            LOG_WARN("{} AX VDEC returned unsupported format {}", name, static_cast<int>(source.enImgFormat));
            throw std::runtime_error("AX VDEC returned unsupported pixel format");
        }
        const auto crop_x = source.s16CropWidth > 0 ? source.s16CropX : 0;
        const auto crop_y = source.s16CropHeight > 0 ? source.s16CropY : 0;
        const auto width =
            source.s16CropWidth > 0 ? static_cast<size_t>(source.s16CropWidth) : source.u32Width;
        const auto height =
            source.s16CropHeight > 0 ? static_cast<size_t>(source.s16CropHeight) : source.u32Height;
        if (crop_x < 0 || crop_y < 0 || (crop_x & 1) || (crop_y & 1) || width == 0 || height == 0 ||
            (width & 1U) || (height & 1U) || static_cast<size_t>(crop_x) + width > source.u32Width ||
            static_cast<size_t>(crop_y) + height > source.u32Height ||
            source.stCompressInfo.enCompressMode != AX_COMPRESS_MODE_NONE) {
            LOG_WARN("{} AX VDEC rejected invalid crop or compressed frame", name);
            throw std::runtime_error("AX VDEC rejected invalid crop or compression");
        }
        const size_t y_stride   = source.u32PicStride[0];
        const size_t uv_stride  = source.u32PicStride[1];
        const uint64_t y_bytes  = static_cast<uint64_t>(y_stride) * source.u32Height;
        const uint64_t uv_bytes = static_cast<uint64_t>(uv_stride) * (source.u32Height / 2);
        if (y_stride < source.u32Width || uv_stride < source.u32Width || !source.u64PhyAddr[0] ||
            !source.u64PhyAddr[1] || y_bytes > std::numeric_limits<AX_U32>::max() ||
            uv_bytes > std::numeric_limits<AX_U32>::max() || source.u32FrameSize < y_bytes + uv_bytes) {
            LOG_WARN("{} AX VDEC rejected frame planes or extents", name);
            throw std::runtime_error("AX VDEC rejected frame planes or extents");
        }
        const size_t y_offset  = static_cast<size_t>(crop_y) * y_stride + static_cast<size_t>(crop_x);
        const size_t uv_offset = static_cast<size_t>(crop_y / 2) * uv_stride + static_cast<size_t>(crop_x);
        return {original_index,
                width,
                height,
                y_stride,
                uv_stride,
                static_cast<AX_U32>(y_bytes),
                static_cast<AX_U32>(uv_bytes),
                y_offset,
                uv_offset,
                vu};
    }

    VideoFramePtr CopyHostFrame(const AxeraDecoderState& state, const HostFrameLayout& layout,
                                const std::string& name) {
        const auto& source = state.held_frame.stVFrame;
        MappedPlane y(source.u64PhyAddr[0], layout.y_bytes);
        MappedPlane uv(source.u64PhyAddr[1], layout.uv_bytes);
        if (!y.Get() || !uv.Get()) {
            LOG_WARN("{} AX VDEC unable to map decoded frame", name);
            throw std::runtime_error("AX VDEC unable to map decoded frame");
        }
        auto output = std::make_shared<VideoFrame>(static_cast<int>(layout.width),
                                                   static_cast<int>(layout.height), PixelFormat::PIXEL_I420);
        if (!output->Active())
            throw std::runtime_error("AX VDEC output frame allocation failed");
        if (!CopyAxeraSemiplanarToI420(y.Get() + layout.y_offset, layout.y_bytes - layout.y_offset,
                                       layout.y_stride, uv.Get() + layout.uv_offset,
                                       layout.uv_bytes - layout.uv_offset, layout.uv_stride, layout.width,
                                       layout.height, layout.vu, output->GetData(), output->GetSize())) {
            LOG_WARN("{} AX VDEC unable to copy decoded frame", name);
            throw std::runtime_error("AX VDEC frame copy failed");
        }
        output->SetFrameIndex(layout.index);
        return output;
    }

    VideoFramePtr CopyAndReleaseHostFrame(AxeraDecoderState& state, FrameLease& lease,
                                          const HostFrameLayout& layout, const std::string& name) {
        auto output = CopyHostFrame(state, layout, name);
        if (!lease.Release())
            throw std::runtime_error("AX VDEC frame release failed");
        return output;
    }

    // A host output retains only this decoder's single held slot. It stays on
    // the decoder thread and never exports an AX buffer to another consumer.
    class DeferredHostFrame {
    public:
        DeferredHostFrame(std::shared_ptr<AxeraDecoderState> state, HostFrameLayout layout,
                          const std::string& name)
            : state_(std::move(state)), layout_(layout), name_(name) {}
        ~DeferredHostFrame() {
            Release();
        }
        void Arm() noexcept {
            active_ = true;
        }
        VideoFramePtr Materialize() {
            std::lock_guard<std::mutex> lock(state_->mutex);
            active_ = false;
            FrameLease lease(*state_);
            try {
                return CopyAndReleaseHostFrame(*state_, lease, layout_, name_);
            } catch (...) {
                state_->fault = true;
                throw;
            }
        }
        void Discard() {
            if (!Release())
                throw std::runtime_error("AX VDEC discarded host frame release failed");
        }

    private:
        bool Release() noexcept {
            if (!active_)
                return true;
            std::lock_guard<std::mutex> lock(state_->mutex);
            active_ = false;
            return state_->ReleaseHeldFrame();
        }

        std::shared_ptr<AxeraDecoderState> state_;
        HostFrameLayout layout_;
        std::string name_;
        bool active_{false};
    };

}  // namespace

VideoFramePtr VideoDecoderAxera::GetFrame() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!FetchHostFrame(*state_, idx_name_))
        return nullptr;
    FrameLease lease(*state_);
    try {
        return CopyAndReleaseHostFrame(*state_, lease, DescribeHostFrame(*state_, idx_name_), idx_name_);
    } catch (...) {
        state_->fault = true;
        throw;
    }
}

void VideoDecoderAxera::RequireNativeFrames() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->native_only = true;
}

bool VideoDecoderAxera::NativeOnly() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->native_only;
}

bool VideoDecoderAxera::NativeClosing() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->native_closing;
}

bool VideoDecoderAxera::PrepareNativeInput() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->native_closing)
        return false;
    const bool ready = state_->native_only && !state_->fault && state_->DrainReturnedNativeFrame() &&
                       !(state_->NativeOutstanding() >= state_->native_capacity);
    if (!ready && state_->trace)
        ++state_->trace_input_wait_polls;
    if (!ready && state_->diag_counts[13] < 3 && ++state_->diag_counts[13] > 0)
        LOG_WARN("{} AX VDEC diag native input unavailable: native={} fault={} lease={}", idx_name_,
                 state_->native_only, state_->fault,
                 (state_->NativeOutstanding() >= state_->native_capacity));
    return ready;
}

bool VideoDecoderAxera::PrepareNativeSelection() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->transient_selection)
        return true;
    if (state_->native_closing || state_->fault || !state_->native_only ||
        state_->native_owner_thread != std::this_thread::get_id() || !state_->DrainReturnedNativeFrame())
        return false;
    // The currently selected frame is one of these leases. Before handing it
    // downstream, make room so at most four ordinary leases remain in flight.
    const bool ready = state_->NativeOutstanding() <= 4;
    if (state_->trace) {
        if (state_->trace_last_fifth) {
            ++state_->trace_selected;
            state_->trace_last_fifth = false;
        }
        if (!ready) {
            ++state_->trace_select_wait_polls;
            if (!state_->trace_selected_pending)
                state_->trace_selection_started = std::chrono::steady_clock::now();
            state_->trace_selected_pending = true;
        } else if (state_->trace_selected_pending) {
            const auto ns =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now() - state_->trace_selection_started)
                                          .count());
            ++state_->trace_selection_complete;
            state_->trace_selection_wait_ns += ns;
            state_->trace_selection_max_ns = std::max(state_->trace_selection_max_ns, ns);
            state_->trace_selected_pending = false;
        }
    }
    return ready;
}

DecodedVideoFrame VideoDecoderAxera::GetDecodedFrame() {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->native_only) {
            if (!FetchHostFrame(*state_, idx_name_))
                return {};
            FrameLease held(*state_);
            try {
                const auto layout = DescribeHostFrame(*state_, idx_name_);
                auto lease        = std::make_shared<DeferredHostFrame>(state_, layout, idx_name_);
                DecodedVideoFrame frame(
                    layout.index, layout.width, layout.height, PixelFormat::PIXEL_I420,
                    [lease]() { return lease->Materialize(); }, [lease]() { lease->Discard(); });
                // Keep the stack lease armed until both callbacks exist.
                lease->Arm();
                held.Disarm();
                return frame;
            } catch (...) {
                state_->fault = true;
                throw;
            }
        }
    }

    auto native = GetNativeFrame();
    if (!native)
        return {};
    // The native-only frame deliberately has no CPU materialization route.
    // The exporter owns the VDEC lease until all copies of the descriptor
    // are dropped; SDK release remains on the decoder owner thread.
    return DecodedVideoFrame(
        native->frame_index, static_cast<size_t>(native->width), static_cast<size_t>(native->height),
        native->format == NativeVideoBufferFormat::NV12 ? PixelFormat::PIXEL_NV12 : PixelFormat::PIXEL_NV21,
        []() -> VideoFramePtr { return nullptr; }, []() {}, [native]() { return native; });
}

NativeVideoBufferPtr VideoDecoderAxera::GetNativeFrame() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->native_owner_thread != std::thread::id{} &&
        state_->native_owner_thread != std::this_thread::get_id())
        throw std::runtime_error("AX VDEC native frame access requires its owner thread");
    state_->native_owner_thread = std::this_thread::get_id();
    if (state_->native_closing)
        return nullptr;
    if (!state_->DrainReturnedNativeFrame())
        throw std::runtime_error("AX VDEC native frame release failed");
    if (state_->fault)
        throw std::runtime_error("AX VDEC is faulted; close and reopen");
    if (!state_->receiving)
        return nullptr;
    if (!state_->native_pool) {
#ifdef COSMO_AXERA_SCENARIOBENCH_CANDIDATE
        const char* requested = std::getenv("COSMO_AXERA_VDEC_MAX_NATIVE_LEASES");
        if (requested && std::strcmp(requested, "4") == 0) {
            state_->native_capacity     = 4;
            const char* reorder         = std::getenv("COSMO_AXERA_VDEC_OUT_OF_ORDER_RELEASE");
            state_->native_out_of_order = reorder && std::strcmp(reorder, "1") == 0;
            const char* transient       = std::getenv("COSMO_AXERA_VDEC_TRANSIENT_SELECTION");
            const char* bench           = std::getenv("COSMO_AXERA_BENCH_CROSS_CHANNEL");
            state_->transient_selection = state_->native_out_of_order && transient &&
                                          std::strcmp(transient, "1") == 0 && bench &&
                                          std::strcmp(bench, "1") == 0;
            if (state_->transient_selection)
                state_->native_capacity = 5;
        }
#endif
        state_->native_pool =
            std::make_unique<AxeraDecoderState::NativePool>(state_, state_->native_capacity);
    }
    if (state_->trace)
        state_->trace_last_fifth = false;
    const bool trace_fifth = state_->trace && state_->transient_selection && state_->NativeOutstanding() >= 4;
    auto reservation       = state_->native_pool->TryReserve();
    if (!reservation) {
        if (state_->native_pool->GetStatus().fault) {
            state_->fault = true;
            throw std::runtime_error("AX VDEC native frame reservation failed");
        }
        return nullptr;
    }
    // The SDK writes into the final stable per-lease slot, never a shared
    // descriptor that can be overwritten by a later frame.
    auto& sdk_frame = reservation->FrameData();
    AX_S32 result   = -1;
    const auto trace_get_start =
        state_->trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    try {
        result = AX_VDEC_GetChnFrame(state_->group, kOutputChannel, &sdk_frame, 50);
    } catch (...) {
        reservation->PoisonUncertain();
        state_->fault = true;
        throw;
    }
    if (state_->trace) {
        ++state_->trace_get_calls;
        state_->trace_get_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now() - trace_get_start)
                                    .count();
        if (result == AX_ERR_VDEC_BUF_EMPTY || result == AX_ERR_VDEC_QUEUE_EMPTY ||
            result == AX_ERR_VDEC_TIMED_OUT)
            ++state_->trace_get_empty;
        if (trace_fifth && result == 0) {
            ++state_->trace_fifth_get;
            state_->trace_last_fifth = true;
        }
    }
    if (result == AX_ERR_VDEC_BUF_EMPTY || result == AX_ERR_VDEC_QUEUE_EMPTY ||
        result == AX_ERR_VDEC_TIMED_OUT) {
        if (!reservation->CancelEmpty()) {
            state_->fault = true;
            throw std::runtime_error("AX VDEC empty frame cancellation failed");
        }
        if (state_->diag_counts[15] < 3 && ++state_->diag_counts[15] > 0)
            LOG_WARN("{} AX VDEC diag native GetChnFrame no output: rc={} group={}", idx_name_, result,
                     state_->group);
        return nullptr;
    }
    if (result != 0) {
        reservation->PoisonUncertain();
        state_->fault = true;
        if (state_->diag_counts[12] < 3 && ++state_->diag_counts[12] > 0)
            LOG_WARN("{} AX VDEC diag native GetChnFrame failed: rc={} group={}", idx_name_, result,
                     state_->group);
        throw std::runtime_error("AX VDEC native frame retrieval failed");
    }
    auto owner = reservation->Publish();
    if (!owner) {
        state_->fault = true;
        throw std::runtime_error("AX VDEC native frame publication failed");
    }
    std::shared_ptr<NativeVideoBuffer> output;
    try {
        const auto& frame = sdk_frame.stVFrame;
        const auto input  = state_->pending_inputs.find(frame.u64PTS);
        if (input == state_->pending_inputs.end())
            throw std::runtime_error("AX VDEC native frame has unknown PTS");
        const bool nv12  = frame.enImgFormat == AX_FORMAT_YUV420_SEMIPLANAR;
        const bool nv21  = frame.enImgFormat == AX_FORMAT_YUV420_SEMIPLANAR_VU;
        const int crop_x = frame.s16CropWidth > 0 ? frame.s16CropX : 0;
        const int crop_y = frame.s16CropHeight > 0 ? frame.s16CropY : 0;
        const size_t width =
            frame.s16CropWidth > 0 ? static_cast<size_t>(frame.s16CropWidth) : frame.u32Width;
        const size_t height =
            frame.s16CropHeight > 0 ? static_cast<size_t>(frame.s16CropHeight) : frame.u32Height;
        const uint64_t y_bytes  = static_cast<uint64_t>(frame.u32PicStride[0]) * frame.u32Height;
        const uint64_t uv_bytes = static_cast<uint64_t>(frame.u32PicStride[1]) * (frame.u32Height / 2);
        if ((!nv12 && !nv21) || frame.stCompressInfo.enCompressMode != AX_COMPRESS_MODE_NONE ||
            (frame.u32Width & 1U) || (frame.u32Height & 1U) || crop_x < 0 || crop_y < 0 || (crop_x & 1) ||
            (crop_y & 1) || width == 0 || height == 0 || (width & 1U) || (height & 1U) ||
            static_cast<size_t>(crop_x) + width > frame.u32Width ||
            static_cast<size_t>(crop_y) + height > frame.u32Height || frame.u32Width > INT32_MAX ||
            frame.u32Height > INT32_MAX || frame.u32PicStride[0] > INT32_MAX ||
            frame.u32PicStride[0] < frame.u32Width || frame.u32PicStride[1] < frame.u32Width ||
            !frame.u64PhyAddr[0] || !frame.u64PhyAddr[1] || y_bytes > std::numeric_limits<AX_U32>::max() ||
            uv_bytes > std::numeric_limits<AX_U32>::max() ||
            static_cast<uint64_t>(frame.u32FrameSize) < y_bytes + uv_bytes ||
            frame.u64PhyAddr[0] > std::numeric_limits<uint64_t>::max() - y_bytes ||
            frame.u64PhyAddr[1] > std::numeric_limits<uint64_t>::max() - uv_bytes ||
            (frame.u64PhyAddr[0] < frame.u64PhyAddr[1] + uv_bytes &&
             frame.u64PhyAddr[1] < frame.u64PhyAddr[0] + y_bytes))
            throw std::runtime_error("AX VDEC rejected native frame layout");
        output                = std::make_shared<NativeVideoBuffer>();
        output->memory_kind   = NativeVideoMemoryKind::AxeraPhysical;
        output->bytes         = frame.u32FrameSize;
        output->width         = static_cast<int>(width);
        output->height        = static_cast<int>(height);
        output->width_stride  = static_cast<int>(frame.u32PicStride[0]);
        output->height_stride = static_cast<int>(frame.u32Height);
        output->format        = nv12 ? NativeVideoBufferFormat::NV12 : NativeVideoBufferFormat::NV21;
        output->crop_x        = crop_x;
        output->crop_y        = crop_y;
        output->planes[0]     = {frame.u64PhyAddr[0], static_cast<size_t>(y_bytes), frame.u32PicStride[0],
                                 frame.u32BlkId[0]};
        output->planes[1]     = {frame.u64PhyAddr[1], static_cast<size_t>(uv_bytes), frame.u32PicStride[1],
                                 frame.u32BlkId[1]};
        output->plane_count   = 2;
        output->backend_frame = &sdk_frame.stVFrame;
        output->frame_index   = input->second->original_frame_index;
        output->owner         = owner;
        if (state_->color_policy_required) {
            VideoColorAssociation association;
            if (output->frame_index > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
                !state_->color_provenance.Resolve(state_->color_generation, frame.u64PTS,
                                                  static_cast<int64_t>(output->frame_index), association) ||
                association.bitstream_width != static_cast<uint32_t>(output->width) ||
                association.bitstream_height != static_cast<uint32_t>(output->height))
                throw std::runtime_error("AX VDEC native frame has no trusted color association");
            output->color_space              = association.color_space;
            output->color_range              = association.color_range;
            output->stream_generation        = association.generation;
            output->decoder_token            = association.decoder_token;
            output->source_pts               = association.source_pts;
            output->source_timestamp         = input->second->source_timestamp;
            output->source_arrival_timestamp = input->second->source_arrival_timestamp;
            output->presentation_steady_ms   = input->second->presentation_steady_ms;
            output->frame_witness = std::shared_ptr<const NativeFrameWitness>(new NativeFrameWitness(
                *output, association.generation, association.decoder_token, association.source_pts));
            if (!output->frame_witness->Matches(*output))
                throw std::runtime_error("AX VDEC native frame witness mismatch");
        }
#ifdef COSMO_AXERA_FACE_FEATURE_ROUTE_CANDIDATE
        else if (codec_type_ == VideoCodecType::kMjpeg && input->second->jfif_admitted) {
            if (frame.stColorGamut != AX_COLOR_GAMUT_BT601 || frame.stDynamicRange != AX_DYNAMIC_RANGE_SDR8 ||
                output->width != 1920 || output->height != 1080 ||
                frame.u64PTS > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                throw std::runtime_error("AX VDEC rejected JFIF frame profile");
            static std::atomic<uint64_t> next_jfif_token{1};
            auto token = next_jfif_token.load();
            while (token && token < std::numeric_limits<uint64_t>::max() &&
                   !next_jfif_token.compare_exchange_weak(token, token + 1)) {
            }
            if (!token || token == std::numeric_limits<uint64_t>::max())
                throw std::runtime_error("JFIF token exhausted");
            output->color_space       = NativeVideoColorSpace::Bt601;
            output->color_range       = NativeVideoColorRange::Full;
            output->stream_generation = token;
            output->decoder_token     = token;
            output->source_pts        = static_cast<int64_t>(frame.u64PTS);
            output->frame_witness     = std::shared_ptr<const NativeFrameWitness>(
                new NativeFrameWitness(*output, token, token, output->source_pts));
            if (!output->frame_witness->Matches(*output))
                throw std::runtime_error("JFIF frame witness mismatch");
        }
#endif
        if (!output->Valid())
            throw std::runtime_error("AX VDEC rejected native frame descriptor");
        return output;
    } catch (...) {
        // The acquired frame is known: release only after both the output and
        // any partially constructed witness have dropped their owner copies.
        output.reset();
        owner.reset();
        if (!state_->DrainReturnedNativeFrame())
            state_->fault = true;
        state_->fault = true;
        throw;
    }
}

}  // namespace cosmo::media
