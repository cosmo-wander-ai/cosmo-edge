// AlgChannelDecode — video decoding, color conversion and frame distribution.
// Image capture and viewer distribution are in AlgChannelDecodeCapture.cc.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <thread>

#include "flow/channel/AlgChannel.h"
#include "flow/channel/NativeLeaseWait.h"
#include "media/VideoFrame.h"
#ifdef COSMO_MEDIA_USE_AXERA_BACKEND
#include "media/VideoDecoderAxera.h"
#if defined(COSMO_NN_USE_AXERA_BACKEND)
#include <limits>

#include "ax_sys_api.h"
#include "media/AxeraVideoFrameCopy.h"
#endif
#endif
#if defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
#include "nn/device/axera/axera_face_diagnostics.h"
#endif
#include "mem/IDeviceContext.h"
#include "nn/core/inference_pipeline_metrics.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/media/IVideoFrameTransform.h"
#include "service/system/IAppInfoService.h"
#include "service/system/IConfigReadService.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

namespace chrono = std::chrono;

static constexpr const char* kTag = "ALGCHANNEL ";
namespace cosmo {
namespace {

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    // Called on the decoder thread while its VDEC frame lease is still live.
    VideoFramePtr CopyNativePreview(const media::NativeVideoBufferPtr& native, int64_t timestamp,
                                    int64_t stream_index) {
        if (!native || !native->Valid() ||
            native->memory_kind != media::NativeVideoMemoryKind::AxeraPhysical ||
            native->format != media::NativeVideoBufferFormat::NV12 || native->crop_x || native->crop_y ||
            !native->frame_witness || !native->frame_witness->Matches(*native) ||
            native->planes[0].bytes > std::numeric_limits<AX_U32>::max() ||
            native->planes[1].bytes > std::numeric_limits<AX_U32>::max())
            return nullptr;
        struct Mapping {
            AX_VOID* ptr;
            AX_U32 bytes;
            ~Mapping() {
                if (ptr && ptr != reinterpret_cast<AX_VOID*>(-1))
                    AX_SYS_Munmap(ptr, bytes);
            }
        };
        Mapping y{
            AX_SYS_Mmap(native->planes[0].physical_address, static_cast<AX_U32>(native->planes[0].bytes)),
            static_cast<AX_U32>(native->planes[0].bytes)};
        Mapping uv{
            AX_SYS_Mmap(native->planes[1].physical_address, static_cast<AX_U32>(native->planes[1].bytes)),
            static_cast<AX_U32>(native->planes[1].bytes)};
        if (!y.ptr || y.ptr == reinterpret_cast<AX_VOID*>(-1) || !uv.ptr ||
            uv.ptr == reinterpret_cast<AX_VOID*>(-1))
            return nullptr;
        auto frame = std::make_shared<media::VideoFrame>(
            native->width, native->height, media::PixelFormat::PIXEL_I420, native->frame_index, timestamp);
        if (!frame->Active() || !media::CopyAxeraSemiplanarToI420(
                                    static_cast<const uint8_t*>(y.ptr), y.bytes, native->planes[0].stride,
                                    static_cast<const uint8_t*>(uv.ptr), uv.bytes, native->planes[1].stride,
                                    native->width, native->height, false, frame->GetData(), frame->GetSize()))
            return nullptr;
        frame->SetStreamIndex(stream_index);
        return frame;
    }
#endif

    bool NativeInferenceBufferEnabled() {
#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
        const char* raw = std::getenv("COSMO_RKNN_MPP_DMABUF");
        if (!raw || *raw == '\0') {
            return true;
        }
        std::string value(raw);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value != "0" && value != "false" && value != "off" && value != "no";
#else
        return false;
#endif
    }

}  // namespace

AlgChannelDecode::~AlgChannelDecode() {
    LOG_INFO("ChannelDecode:{}/{} Delete", channel_id_, uuid_);
}

AlgChannelDecode::AlgChannelDecode(AlgChannel& channel_inst, const std::string& channel_id)
    : AlgDataQueueDistributor(channel_id + " Decode"),
      util::Thread(channel_id + " Decode"),
      channel_inst_(channel_inst),
      channel_id_(channel_id),
      duration_stat_(channel_id + " Decode"),
      dec_queue_(std::make_shared<AlgDataQueue<AlgDataPtr>>(channel_id + " Decode", 100)) {
    action_status_ = util::ErrorEnum::NotInit;
    device_id_ =
        static_cast<int>(service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetNumber());
    uuid_         = util::GenerateUUID();
    decoder_name_ = channel_id + " Decode";
    LOG_INFO("ChannelDecode:{}/{} Init (decoder deferred)", channel_id_, uuid_);
}

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
bool AlgChannelDecode::SetNativeFaceRequiredBeforeStart(bool required) {
    if (is_running_.load() || !native_face_stopped_cleanly_.load())
        return false;
    native_source_dimensions_.store(0, std::memory_order_release);
    native_face_required_ = required;
    return true;
}
#endif

bool AlgChannelDecode::Start() {
    action_status_ = util::ErrorEnum::ActionStart;
    is_running_    = true;
    dec_queue_->Resume();  // Ensure queue is usable (marked unavailable after Quit/Stop).
    ResetDistributor();
    // Reset frame tracking state to prevent residual seq/stream from the
    // previous round from affecting frame validation and decoder lifecycle.
    frame_index_  = -1;
    stream_index_ = -1;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    native_presentation_order_.Reset();
#endif
    decode_count_                = 0;
    codec_reset_sign_            = false;
    cap_image_stream_index_      = -1;
    consecutive_decode_failures_ = 0;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_)
        native_face_stopped_cleanly_ = false;
#endif
    bool started                   = false;
    bool creation_threw            = false;
    const bool previously_joinable = joinable();
    try {
        started = start();
    } catch (const std::system_error&) {
        creation_threw = true;
    } catch (...) {
        // Unknown failure: keep native state quarantined.
    }
    if (!started) {
        is_running_ = false;
        dec_queue_->Stop();
        stop();
        action_status_ = util::ErrorEnum::ActionStop;
        LOG_ERRO("ChannelDecode:{}/{} Start failed: previous thread still joinable", channel_id_, uuid_);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_face_required_ && creation_threw && !previously_joinable && !decoder_)
            native_face_stopped_cleanly_ = true;
#else
        (void)creation_threw;
        (void)previously_joinable;
#endif
        return false;
    }
    return true;
}

bool AlgChannelDecode::Stop() {
    is_running_    = false;
    action_status_ = util::ErrorEnum::ActionStop;
    dec_queue_->Stop();
    stop();
    if (joinable())
        return false;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_ && !native_face_stopped_cleanly_)
        return false;
#endif
    return true;
}

void AlgChannelDecode::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                                   unsigned int duration_sec) {
    AlgActionDataQueueStatus status;
    auto duration_info = duration_stat_.ComputeStats();
    status.durationInfos.push_back(duration_info);
    status.actionId = std::string(BAStreamChannel_Code) + " DECODE";
    if (dec_queue_->Status(status.queueStatus, duration_sec)) {
        status.channelIds.push_back(channel_id_);
        status.actionStatus = action_status_;

        que_status.push_back(status);
    }
    return;
}

void AlgChannelDecode::ActionInfo(std::vector<ActionRuntimeInfo>& action_infos) {
    auto bind_tasks = GetBindTasks();
    for (auto& bind_task : bind_tasks) {
        ActionRuntimeInfo action_info_el;
        action_info_el.actionId   = std::string(BAStreamChannel_Code) + " DECODE";
        action_info_el.maxTaskFps = bind_task.max_task_fps;
        if (bind_task.que)
            action_info_el.queueName = bind_task.que->Name();
        for (auto& task : bind_task.tasks) {
            ActionRuntimeSon son;
            son.channelId = task.channel_id;
            son.taskId    = task.task_id;
            son.actionId  = task.actionId;
            son.fps       = task.fps;
            if (task.que)
                son.queueName = task.que->Name();
            action_info_el.sons.push_back(son);
        }
        action_infos.push_back(action_info_el);
    }
}

bool AlgChannelDecode::NeedsResize(VideoPacketPtr& video_frame) {
    return (video_frame->GetWidth() * video_frame->GetHeight()) >
           (media::kVideoDefaultWidth * media::kVideoDefaultHeight);
}

bool AlgChannelDecode::ValidateFrame(VideoPacketPtr& video_frame, bool just_need_i_frame) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_ && stream_index_ == video_frame->stream_idx && frame_index_ >= 0 &&
        video_frame->index != frame_index_ + 1)
        codec_reset_sign_ = true;
    if (native_face_required_ && codec_reset_sign_ && !video_frame->IsIFrame()) {
        dec_queue_->RecordDiscard();
        return false;
    }
#endif
    if (!just_need_i_frame) {
        if ((frame_index_ < 0) || (stream_index_ > video_frame->stream_idx)) {
            if (!video_frame->IsIFrame()) {
                return false;
            }
            LOG_INFO("{} Got I Frame At {}, Last Frame:{}", decoder_name_, video_frame->index, frame_index_);
        } else {
            if (video_frame->index <= 0) {
                if (!video_frame->IsIFrame()) {
                    return false;
                }
                LOG_INFO("{} Got I Frame At {}, Last Frame:{}", decoder_name_, video_frame->index,
                         frame_index_);
            } else if ((frame_index_ + 1) != video_frame->index) {
                if (!video_frame->IsIFrame()) {
                    return false;
                }
                LOG_WARN("{} Got I Frame At {}, Last Frame:{}", decoder_name_, video_frame->index,
                         frame_index_);
            }
        }
    }
    return true;
}

void AlgChannelDecode::PrepareDecoder(VideoPacketPtr& video_frame) {
    if (video_frame->codec_type == media::VideoCodecType::kMjpeg) {
        return;  // MJPEG frames are single JPEGs decoded via DecodeJpeg, not the VPU decoder.
    }
    // Lazily create decoder instance (allocate VPU VRAM only on first use).
    if (!decoder_) {
        LOG_INFO("{} Creating video decoder on demand (deviceId={})", decoder_name_, device_id_);
        auto* media_handle = service::ServiceRegistry::Instance().Get<mem::IDeviceContext>().GetMediaHandle();
        decoder_           = media::VideoDecoder::Create(static_cast<size_t>(device_id_), media_handle);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_face_required_) {
            auto* ax = dynamic_cast<media::VideoDecoderAxera*>(decoder_.get());
            if (!ax)
                return;
            ax->RequireNativeFrames();
        }
#endif
    }
    if (!decoder_)
        return;
    const auto close_decoder = [this]() {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_face_required_) {
            // Keep this packet and its keyframe while outstanding decoder
            // leases return. Stop interrupts the bounded wait.
            return WaitForNativeInput(
                [&]() { return is_running_.load(); }, [&]() { return decoder_->Close(); },
                []() { return chrono::steady_clock::now(); },
                []() { std::this_thread::sleep_for(chrono::milliseconds(10)); }, chrono::seconds(2));
        }
#endif
        return decoder_->Close();
    };
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    auto* native_decoder =
        native_face_required_ ? dynamic_cast<media::VideoDecoderAxera*>(decoder_.get()) : nullptr;
    if (native_decoder && native_decoder->NativeClosing() && !close_decoder())
        return;
    if (native_face_required_ && codec_reset_sign_) {
        // Reset poisons provenance on the old instance. Close successfully
        // on the owner thread and create a new parser for this stream epoch.
        if (!close_decoder())
            return;
        decoder_.reset();
        auto* media_handle = service::ServiceRegistry::Instance().Get<mem::IDeviceContext>().GetMediaHandle();
        decoder_           = media::VideoDecoder::Create(static_cast<size_t>(device_id_), media_handle);
        auto* ax           = dynamic_cast<media::VideoDecoderAxera*>(decoder_.get());
        if (!ax)
            return;
        ax->RequireNativeFrames();
    }
#endif
    const bool needs_resize = NeedsResize(video_frame);
    const int decoder_width = needs_resize ? media::kVideoDefaultWidth : static_cast<int>(video_frame->width);
    const int decoder_height =
        needs_resize ? media::kVideoDefaultHeight : static_cast<int>(video_frame->height);
    const bool stream_restarted = stream_index_ != video_frame->stream_idx;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_ && stream_restarted)
        native_source_dimensions_.store(0, std::memory_order_release);
#endif

    // A backend may retain a compatible hardware context across a clean
    // keyframe boundary. CPU/Sophon keep their existing Close/Open behavior.
    if (stream_restarted && !codec_reset_sign_ &&
        decoder_->ReuseForStreamRestart(video_frame->codec_type, decoder_width, decoder_height)) {
        frame_info_.clear();
        stream_index_ = video_frame->stream_idx;
        decode_count_ = 0;
        frame_index_  = -1;
        ++decoder_stream_reuse_count_;
        if (decoder_stream_reuse_count_ == 1 || decoder_stream_reuse_count_ % 240 == 0) {
            LOG_INFO("{} reused decoder across stream restart: stream={} codec={} size={}x{} count={}",
                     decoder_name_, stream_index_, video_frame->codec_type, decoder_width, decoder_height,
                     decoder_stream_reuse_count_);
        }
        return;
    }

    // Rebuild decoder on an incompatible stream restart or exception.
    if (stream_restarted || codec_reset_sign_) {
        if (decoder_->IsOpened()) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
            if (native_face_required_) {
                if (!close_decoder())
                    return;
            } else
#endif
                (void)close_decoder();
            LOG_INFO("{} Decoder Reset Last stream:{} New Stream:{} SuccessCount:{} codecResetSign:{} ",
                     decoder_name_, stream_index_, video_frame->stream_idx, decode_count_, codec_reset_sign_);
        }
    }
    if (!decoder_->IsOpened()) {
        frame_info_.clear();
        stream_index_               = video_frame->stream_idx;
        codec_reset_sign_           = false;
        decode_count_               = 0;
        frame_index_                = -1;
        decoder_stream_reuse_count_ = 0;
        LOG_INFO("{} streamIndex:{} videoType:{} Changed, dumux video width:{}, height:{}", decoder_name_,
                 stream_index_, video_frame->codec_type, video_frame->width, video_frame->height);

        decoder_->SetCodecType(video_frame->codec_type, decoder_width, decoder_height);
        decoder_->Open();
    }
}

void AlgChannelDecode::HandFrame(AlgDataPtr demux_data) {
    if (AlgDataType::ChannelDataOrig != demux_data->dataType) {
        action_status_ = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    auto video_frame = demux_data->chanDataOrig.packet;
    if (!video_frame) {
        LOG_INFO("{} Empty Data", decoder_name_);
        action_status_ = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!media::IsValidVideoResolution(static_cast<int>(video_frame->GetWidth()),
                                       static_cast<int>(video_frame->GetHeight()))) {
        action_status_ = util::ErrorEnum::VideoResolutionNotSupport;
        return;
    }

    bool just_need_i_frame = (GetMaxFps() < 1.0f) && (viewer_queue_.empty());
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_) {
        // Decoding only I-frames breaks parser state and reordered VDEC tokens.
        just_need_i_frame = false;
        if (video_frame->codec_type != media::VideoCodecType::kH264) {
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            return;
        }
    }
#endif

    if (!ValidateFrame(video_frame, just_need_i_frame)) {
        return;
    }

    // When inference is slower than input, backlog quickly amplifies VRAM usage in decode/convert.
    // Keep keyframes and drop part of non-I frames to stabilize memory.
    // if ((dec_queue_->RestSize() >= kDecodeBacklogDropThreshold) && (!video_frame->IsIFrame())) {
    //     dec_queue_->RecordDiscard();
    // }

#ifdef COSMO_MEDIA_USE_AXERA_BACKEND
    const auto* ax_existing = dynamic_cast<media::VideoDecoderAxera*>(decoder_.get());
    if (ax_existing && ax_existing->NativeOnly() &&
        video_frame->codec_type == media::VideoCodecType::kMjpeg) {
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        LOG_WARN("{} AX native-only channel rejects MJPEG host decoding", decoder_name_);
        return;
    }
#endif
    PrepareDecoder(video_frame);

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    auto* strict_decoder =
        native_face_required_ ? dynamic_cast<media::VideoDecoderAxera*>(decoder_.get()) : nullptr;
    // A close still waiting on an old lease is not a new-stream decoder
    // failure. Never send the new epoch to that old decoder.
    if (native_face_required_ && strict_decoder &&
        (strict_decoder->NativeClosing() || stream_index_ != video_frame->stream_idx)) {
        dec_queue_->RecordDiscard();
        return;
    }
    if (native_face_required_ && (!strict_decoder || !strict_decoder->NativeOnly() ||
                                  !strict_decoder->IsOpened() || codec_reset_sign_)) {
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        return;
    }
    if (native_face_required_) {
        const bool ready = WaitForNativeInput(
            [&]() { return is_running_.load(); }, [&]() { return strict_decoder->PrepareNativeInput(); },
            []() { return chrono::steady_clock::now(); },
            []() { std::this_thread::sleep_for(chrono::milliseconds(10)); }, chrono::seconds(2));
        if (!ready) {
            codec_reset_sign_ = true;
            dec_queue_->RecordDiscard();
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            return;
        }
    }
#endif

    if (just_need_i_frame) {
        if (!video_frame->IsIFrame()) {
            return;
        }
        if (0 == decode_count_ % 300) {
            LOG_INFO("{} MaxFps:{} Just Need I Frame, Frame:{} is {} Frame", decoder_name_, GetMaxFps(),
                     video_frame->GetSequence(), video_frame->IsIFrame() ? "I" : "P");
        }
    }

    duration_stat_.BeginSample();
    bool is_decode_ret = false;
    FrameInfoSave(demux_data);
    media::DecodedVideoFrame decoded_frame;
#ifdef TEST_NO_DECODER
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_) {
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        duration_stat_.EndSample();
        return;
    }
#endif
    decoded_frame = media::DecodedVideoFrame(std::make_shared<media::VideoFrame>(1920, 1080));
    is_decode_ret = decoded_frame.HasFrame();
#else
    if (video_frame->codec_type == media::VideoCodecType::kMjpeg) {
        auto frame_data = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().DecodeJpeg(
            std::vector<u_int8_t>(video_frame->data.begin(), video_frame->data.end()));
        if (frame_data) {
            // MJPEG hardware decode returns frames without business-side frame
            // indices; fill in index/stream/timestamp for overlay alignment.
            frame_data->SetFrameIndex(static_cast<uint64_t>(video_frame->index));
            frame_data->SetStreamIndex(video_frame->stream_idx);
            frame_data->SetTimestamp(video_frame->timestamp);
        }
        is_decode_ret = (frame_data != nullptr);
        decoded_frame = media::DecodedVideoFrame(std::move(frame_data));
    } else {
        try {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
            if (native_face_required_)
                decoded_frame = strict_decoder->DecodeNativePacket(*video_frame, is_decode_ret);
            else
#endif
                decoded_frame = decoder_->DecodeFrame(video_frame->data.data(), video_frame->data.size(),
                                                      video_frame->index, is_decode_ret);
        } catch (const std::exception& e) {
            codec_reset_sign_ = true;
            action_status_    = util::ErrorEnum::DecoderFrameFailed;
            duration_stat_.EndSample();
            LOG_ERRO("{} Last Frame is {} Have Decord Errors: {}", decoder_name_, frame_index_, e.what());
            return;
        }
    }
#endif

    if (!decoded_frame.HasFrame()) {
        duration_stat_.EndSample();
        if (is_decode_ret) {
            frame_index_ = video_frame->index;
            // SendPacket succeeded but GetFrame returned no output (VPU internal buffering); not a failure.
        } else {
            consecutive_decode_failures_++;
            if (consecutive_decode_failures_ >= kMaxConsecutiveDecodeFailures) {
                LOG_WARN("{} {} consecutive decode failures (frameIndex:{}), forcing decoder reset",
                         decoder_name_, consecutive_decode_failures_, video_frame->index);
                codec_reset_sign_            = true;
                consecutive_decode_failures_ = 0;
            }
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            LOG_INFO("{} Decode Failed At {}, Last Frame:{}, consecutiveFails:{}", decoder_name_,
                     video_frame->index, frame_index_, consecutive_decode_failures_);
        }
        return;
    }

    const auto decoded_frame_index = decoded_frame.GetFrameIndex();
    const auto frame_info          = FrameInfoGet(static_cast<int64_t>(decoded_frame_index));
    const bool matched_frame_info =
        frame_info.index >= 0 && static_cast<uint64_t>(frame_info.index) == decoded_frame_index;
    int64_t output_timestamp          = matched_frame_info ? frame_info.timestamp : util::GetMilliseconds();
    const int64_t output_stream_index = matched_frame_info ? frame_info.streamIndex : video_frame->stream_idx;

    AlgDataPtr output_data = demux_data;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_) {
        const auto presentation_time = frame_info.presentation_time_valid
                                           ? media::NativeSteadyTimeFromMs(frame_info.presentation_steady_ms)
                                           : std::nullopt;
        if (!matched_frame_info || !presentation_time) {
            decoded_frame.Discard();
            duration_stat_.EndSample();
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            return;
        }
        // Scheduling and result routing must use the decoded output's own
        // demux time, never the packet that happened to trigger this output.
        output_data                   = std::make_shared<AlgData>();
        output_data->dataType         = AlgDataType::ChannelDataOrig;
        output_data->channelId        = channel_id_;
        output_timestamp              = frame_info.presentation_timestamp_ms;
        output_data->firstTimePoint   = *presentation_time;
        output_data->chanDataOrig.fps = frame_info.fps;
        if (video_frame->index == frame_info.index && video_frame->stream_idx == frame_info.streamIndex)
            output_data->chanDataOrig.packet = video_frame;
    }
#endif
    AlgFrameDistributionPlan task_plan;
    const bool prepared_task_distribution = decoded_frame.IsDeferred();
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    // Native presentation order must be validated before FPS/filter state is
    // touched. The native branch prepares its plan after witness validation.
    if (prepared_task_distribution && !native_face_required_)
        task_plan = PrepareFrameDistribution(output_data);
#else
    if (prepared_task_distribution)
        task_plan = PrepareFrameDistribution(output_data);
#endif

#ifdef COSMO_MEDIA_USE_AXERA_BACKEND
    auto* ax_decoder = dynamic_cast<media::VideoDecoderAxera*>(decoder_.get());
    if (ax_decoder && ax_decoder->NativeOnly()) {
#ifdef COSMO_NN_USE_AXERA_BACKEND
        // Keep failure paths on the native side. Viewers and admitted still
        // captures use independent copies of the witnessed original frame.
        const auto native = decoded_frame.ExportNativeBuffer();
        const bool matched_witness =
            matched_frame_info && native && native->Valid() && native->frame_witness &&
            native->frame_witness->Matches(*native) && native->frame_index == decoded_frame_index &&
            native->frame_witness->Generation() == static_cast<std::uint64_t>(output_stream_index) &&
            native->frame_witness->SourceTimestamp() == output_timestamp &&
            native->frame_witness->SourceArrivalTimestamp() == frame_info.timestamp &&
            native->frame_witness->PresentationSteadyMs() == frame_info.presentation_steady_ms;
        if (matched_witness)
            native_source_dimensions_.store((static_cast<std::uint64_t>(native->width) << 32) |
                                                static_cast<std::uint32_t>(native->height),
                                            std::memory_order_release);
        // Reject a reordered/duplicate presentation output for this generation;
        // packet decode order itself is intentionally allowed to move backward.
        if (matched_witness && !native_presentation_order_.Accept(
                                   native->frame_witness->Generation(), native->frame_witness->SourcePts(),
                                   output_timestamp, frame_info.presentation_steady_ms)) {
            decoded_frame.Discard();
            duration_stat_.EndSample();
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            return;
        }
        // Inspection is side-effect free: do not advance viewer/capture or
        // task FPS state for an unsupported native consumer.
        // Capture is still incompatible; admitted viewers receive independent host copies.
        const bool capture_requested      = HasPendingCapture();
        const bool native_capture         = capture_requested && HasE24NativeCapture();
        const bool incompatible_consumers = capture_requested && !native_capture;
        if (matched_witness && native_face_required_ && prepared_task_distribution && !incompatible_consumers)
            task_plan = PrepareFrameDistribution(output_data, native->frame_witness->Generation());
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        if (matched_witness)
            nn::axera::diagnostics::Trace(
                nn::axera::diagnostics::Event::Decoded,
                {channel_id_, native->frame_witness->Generation(), native->frame_index, output_timestamp});
#endif
        // Copy before handing the lease to inference, never queue the VDEC frame itself.
        // Preview failure remains independent of inference delivery.
        if (matched_witness && native_face_required_ && !incompatible_consumers) {
            try {
                const auto preview_plan = PrepareViewerDistribution();
                if (native_capture || !preview_plan.empty()) {
                    auto host = CopyNativePreview(native, output_timestamp, output_stream_index);
                    if (host && native_capture)
                        DoCaptureImage(host);
                    if (host && !preview_plan.empty())
                        DistributePreparedViewer(preview_plan, std::move(host));
                }
            } catch (...) {
                LOG_WARN("{} AX native preview copy failed", decoder_name_);
            }
        }
        if (task_plan.CanDiscardNativeFrame() && IsCurrentPlan(task_plan) && !incompatible_consumers) {
            // No consumer selected this frame (FPS filter or a full queue).
            // Discarding is not an inference success and is not a decode error.
            decoded_frame.Discard();
            duration_stat_.EndSample();
            frame_index_ = video_frame->index;
            ++decode_count_;
            consecutive_decode_failures_ = 0;
            action_status_               = util::ErrorEnum::Success;
            return;
        }
        const bool ready = native_face_required_ && matched_frame_info &&
                           task_plan.SupportsNativeInference() &&
                           task_plan.native_destinations.size() == task_plan.queues.size() &&
                           matched_witness && !NeedsResize(video_frame) && !incompatible_consumers;
        if (ready && strict_decoder &&
            !WaitForNativeInput([&]() { return is_running_.load(); },
                                [&]() { return strict_decoder->PrepareNativeSelection(); },
                                []() { return chrono::steady_clock::now(); },
                                []() { std::this_thread::sleep_for(chrono::milliseconds(2)); },
                                chrono::seconds(2))) {
            decoded_frame.Discard();
            duration_stat_.EndSample();
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            return;
        }
        const auto delivery = ready ? TryDistributorPreparedAxeraFaceFrame(
                                          task_plan, output_data, native, output_timestamp,
                                          output_stream_index, native->frame_witness->Generation())
                                    : AxeraNativeDelivery::Rejected;
        if (delivery == AxeraNativeDelivery::Rejected) {
            decoded_frame.Discard();
            duration_stat_.EndSample();
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            LOG_WARN("{} AX native face handoff rejected", decoder_name_);
            return;
        }
        decoded_frame.Discard();
        duration_stat_.EndSample();
        frame_index_ = video_frame->index;
        ++decode_count_;
        consecutive_decode_failures_ = 0;
        action_status_               = util::ErrorEnum::Success;
        return;
#else
        decoded_frame.Discard();
        duration_stat_.EndSample();
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        return;
#endif
    }
#endif

    media::NativeVideoBufferPtr native_inference_buffer;
    if (task_plan.SupportsNativeInference() && NativeInferenceBufferEnabled()) {
        native_inference_buffer = decoded_frame.ExportNativeBuffer();
    }

    ViewerDistributionPlan viewer_plan;
#if defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND) || defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    // Viewers move their existing FPS filter ahead of Copy-out. The
    // callback also rejects a saturated preview queue before host allocation.
    viewer_plan                                 = PrepareViewerDistribution();
    constexpr bool prepared_viewer_distribution = true;
#else
    constexpr bool prepared_viewer_distribution = false;
#endif

    const bool host_frame_required = !decoded_frame.IsDeferred() || !task_plan.Empty() ||
                                     !viewer_plan.empty() || NeedsHostFrame(output_stream_index);
    const auto take_host_frame = [&]() -> VideoFramePtr {
        if (host_frame_required)
            return decoded_frame.Materialize();
        decoded_frame.Discard();
        return {};
    };
#ifdef COSMO_MEDIA_USE_AXERA_BACKEND
    VideoFramePtr frame_data;
    try {
        frame_data = take_host_frame();
    } catch (const std::exception& e) {
        codec_reset_sign_ = true;
        action_status_    = util::ErrorEnum::DecoderFrameFailed;
        duration_stat_.EndSample();
        LOG_WARN("{} decoded host frame failed at frame:{} stream:{}: {}", decoder_name_, decoded_frame_index,
                 output_stream_index, e.what());
        return;
    }
#else
    auto frame_data = take_host_frame();
#endif
    duration_stat_.EndSample();
    if (!host_frame_required) {
        frame_index_ = video_frame->index;
        decode_count_ += 1;
        consecutive_decode_failures_ = 0;
        action_status_               = util::ErrorEnum::Success;
        return;
    }
    if (!frame_data || !frame_data->Active()) {
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        LOG_WARN("{} decoded frame materialization failed at frame:{} stream:{}", decoder_name_,
                 video_frame->index, video_frame->stream_idx);
        return;
    }
    frame_data->SetTimestamp(output_timestamp);
    frame_data->SetStreamIndex(output_stream_index);

    frame_index_ = video_frame->index;
    decode_count_++;
    consecutive_decode_failures_ = 0;  // Successful decode; reset consecutive failure counter.

    // Apply resize for super-resolution frames; otherwise use as-is.
    VideoFramePtr output_frame = frame_data;
    if (NeedsResize(video_frame)) {
        output_frame = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>().Resize(
            frame_data, media::kVideoDefaultHeight, media::kVideoDefaultWidth);
        if (!output_frame) {
            // Resize failure usually means insufficient device/memory; abort to avoid null-pointer crash.
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            LOG_WARN("{} Resize failed at frame:{} stream:{} (src:{}x{} dst:{}x{})", decoder_name_,
                     frame_data ? static_cast<int64_t>(frame_data->GetFrameIndex()) : int64_t{-1},
                     frame_data ? frame_data->GetStreamIndex() : int64_t{-1},
                     frame_data ? frame_data->GetWidth() : size_t{0},
                     frame_data ? frame_data->GetHeight() : size_t{0}, media::kVideoDefaultWidth,
                     media::kVideoDefaultHeight);
            return;
        }
        output_frame->SetFrameIndex(frame_data->GetFrameIndex());
        output_frame->SetTimestamp(frame_data->GetTimestamp());
        output_frame->SetStreamIndex(frame_data->GetStreamIndex());
    }

    if (!output_frame) {
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        return;
    }
    if (!output_frame->Active()) {
        // VideoFrame created but no valid memory allocated (Active=false when Acquire fails).
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        return;
    }

    if constexpr (prepared_viewer_distribution) {
        DistributePreparedViewer(viewer_plan, output_frame);
    } else {
        DistributeViewer(output_frame);
    }
    DoCaptureImage(output_frame);
    CaptureJpeg(output_frame);
    const auto color_convert = [this, native_inference_buffer](AlgDataPtr frame, VideoFramePtr in_data) {
        return ColorConvert(frame, in_data, native_inference_buffer);
    };
    if (prepared_task_distribution) {
        DistributorPreparedFrame(task_plan, demux_data, output_frame, color_convert);
    } else {
        DistributorData(demux_data, output_frame, color_convert);
    }
}

void AlgChannelDecode::FrameInfoSave(const AlgDataPtr& data) {
    if (!data || !data->chanDataOrig.packet) {
        return;
    }
    const auto& packet = data->chanDataOrig.packet;
    AlgFrameInfo info;
    info.streamIndex               = packet->stream_idx;
    info.index                     = packet->index;
    info.timestamp                 = packet->timestamp;
    info.presentation_time_valid   = packet->presentation_time_valid;
    info.presentation_timestamp_ms = packet->presentation_timestamp_ms;
    info.presentation_steady_ms    = packet->presentation_steady_ms;
    info.firstTimePoint            = data->firstTimePoint;
    info.fps                       = data->chanDataOrig.fps;
    frame_info_.push_back(info);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_) {
        // No output may arrive for malformed or heavily buffered streams.
        // A later output whose record was evicted fails the strict match.
        while (frame_info_.size() > 300)
            frame_info_.pop_front();
    }
#endif
}

AlgFrameInfo AlgChannelDecode::FrameInfoGet(int64_t index) {
    return TakeFrameInfo(frame_info_, index);
}

// Image capture and viewer distribution — moved to AlgChannelDecodeCapture.cc

AlgDataPtr AlgChannelDecode::ColorConvert(AlgDataPtr demux_data, VideoFramePtr in_data,
                                          media::NativeVideoBufferPtr native_buffer) {
    if (!VideoFrameValid(in_data, true)) {
        return nullptr;
    }

    // Keep decoded I420 materialization on the decoder thread. Only the packed
    // AI image can be deferred: it owns this host frame, not the decoder/context.
    const auto convert = [in_data]() {
        const auto convert_started = std::chrono::steady_clock::now();
        auto& transform = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
        VideoFramePtr result;
        const auto pixel_format = in_data->GetPixelFormat();
        if (pixel_format == media::PixelFormat::PIXEL_BGR8) {
            result = in_data;
        } else if (pixel_format == media::PixelFormat::PIXEL_RGB8) {
            auto i420_frame = transform.RGB2I420(in_data);
            if (i420_frame)
                result = transform.I4202BGR(i420_frame);
        } else if (pixel_format == media::PixelFormat::PIXEL_I420) {
            result = transform.I4202BGR(in_data);
        } else {
            LOG_WARN("Unsupported decoded pixel format {}", static_cast<int>(pixel_format));
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - convert_started)
                                 .count();
        nn::GetInferencePipelineMetrics().RecordColorConvert(static_cast<uint64_t>(elapsed));
        return result;
    };
    VideoFramePtr ai_frame;
#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)
    const bool native_matches = native_buffer && native_buffer->Valid() &&
                                native_buffer->width == static_cast<int>(in_data->GetWidth()) &&
                                native_buffer->height == static_cast<int>(in_data->GetHeight()) &&
                                (native_buffer->format == media::NativeVideoBufferFormat::NV12 ||
                                 native_buffer->format == media::NativeVideoBufferFormat::I420);
    if (native_matches && in_data->GetPixelFormat() == media::PixelFormat::PIXEL_I420) {
        ai_frame = std::make_shared<media::VideoFrame>(
            static_cast<int>(in_data->GetWidth()), static_cast<int>(in_data->GetHeight()),
            media::PixelFormat::PIXEL_BGR8, in_data->GetFrameIndex(), in_data->GetTimestamp(), convert);
    } else
#endif
    {
        ai_frame = convert();
    }
    if ((!ai_frame) || (!ai_frame->Active())) {
        action_status_ = util::ErrorEnum::DecoderColorConvertFailed;
        return nullptr;
    }

    ai_frame->SetFrameIndex(in_data->GetFrameIndex());
    ai_frame->SetTimestamp(in_data->GetTimestamp());
    ai_frame->SetStreamIndex(in_data->GetStreamIndex());

    AlgDataPtr data                 = std::make_shared<AlgData>();
    data->chanDataOrig.packet       = demux_data->chanDataOrig.packet;
    data->chanDataOrig.fps          = demux_data->chanDataOrig.fps;
    data->dataType                  = AlgDataType::ChannelDataDec;
    data->chanDataDec.frame         = ai_frame;
    data->chanDataDec.native_buffer = std::move(native_buffer);
    data->channelId                 = channel_id_;

    data->firstTimePoint = demux_data->firstTimePoint;
    action_status_       = util::ErrorEnum::Success;
    return data;
}

void AlgChannelDecode::run() {
    while (is_running_) {
        auto demux_data = dec_queue_->Pop();
        if (demux_data) {
            if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetActionSwitch(
                    "Decode")) {
                HandFrame(demux_data);
            }
        } else {
            dec_queue_->WaitForData(35);
        }
    }

#ifndef TEST_NO_DECODER
    // Ensure decoder is destroyed in the same thread it was used/created to avoid VPU context corruption
    bool decoder_closed = true;
    if (decoder_) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_face_required_) {
            // is_running_ is already false, but downstream may still be
            // dropping its final frame owners during shutdown.
            decoder_closed = WaitForNativeInput(
                []() { return true; }, [&]() { return decoder_->Close(); },
                []() { return chrono::steady_clock::now(); },
                []() { std::this_thread::sleep_for(chrono::milliseconds(10)); }, chrono::seconds(2));
        } else
#endif
            decoder_closed = decoder_->Close();
        decoder_.reset();
    }
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_)
        native_face_stopped_cleanly_ = decoder_closed;
#else
    (void)decoder_closed;
#endif
#else
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_required_)
        native_face_stopped_cleanly_ = true;
#endif
#endif

    LOG_INFO("{} THREAD [{}] Stop ", decoder_name_, Name());
}
}  // namespace cosmo
