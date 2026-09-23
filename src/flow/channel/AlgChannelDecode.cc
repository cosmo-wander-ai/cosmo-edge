// AlgChannelDecode — video decoding, color conversion and frame distribution.
// Image capture and viewer distribution are in AlgChannelDecodeCapture.cc.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#include "flow/channel/AlgChannel.h"
#include "media/VideoFrame.h"
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
    uuid_ = util::GenerateUUID();
    name_ = channel_id + " Decode";
    LOG_INFO("ChannelDecode:{}/{} Init (decoder deferred)", channel_id_, uuid_);
}

void AlgChannelDecode::Start() {
    action_status_ = util::ErrorEnum::ActionStart;
    is_running_    = true;
    dec_queue_->Resume();  // Ensure queue is usable (marked unavailable after Quit/Stop).
    ResetDistributor();
    // Reset frame tracking state to prevent residual seq/stream from the
    // previous round from affecting frame validation and decoder lifecycle.
    frame_index_                 = -1;
    stream_index_                = -1;
    decode_count_                = 0;
    codec_reset_sign_            = false;
    cap_image_stream_index_      = -1;
    consecutive_decode_failures_ = 0;
    if (!start()) {
        is_running_    = false;
        action_status_ = util::ErrorEnum::ActionStop;
        LOG_ERRO("ChannelDecode:{}/{} Start failed: previous thread still joinable", channel_id_, uuid_);
    }
}

void AlgChannelDecode::Stop() {
    is_running_    = false;
    action_status_ = util::ErrorEnum::ActionStop;
    dec_queue_->Stop();
    stop();
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
    if (!just_need_i_frame) {
        if ((frame_index_ < 0) || (stream_index_ > video_frame->stream_idx)) {
            if (!video_frame->IsIFrame()) {
                return false;
            }
            LOG_INFO("{} Got I Frame At {}, Last Frame:{}", name_, video_frame->index, frame_index_);
        } else {
            if (video_frame->index <= 0) {
                if (!video_frame->IsIFrame()) {
                    return false;
                }
                LOG_INFO("{} Got I Frame At {}, Last Frame:{}", name_, video_frame->index, frame_index_);
            } else if ((frame_index_ + 1) != video_frame->index) {
                if (!video_frame->IsIFrame()) {
                    return false;
                }
                LOG_WARN("{} Got I Frame At {}, Last Frame:{}", name_, video_frame->index, frame_index_);
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
        LOG_INFO("{} Creating video decoder on demand (deviceId={})", name_, device_id_);
        auto* media_handle = service::ServiceRegistry::Instance().Get<mem::IDeviceContext>().GetMediaHandle();
        decoder_           = media::VideoDecoder::Create(static_cast<size_t>(device_id_), media_handle);
    }
    const bool needs_resize = NeedsResize(video_frame);
    const int decoder_width = needs_resize ? media::kVideoDefaultWidth : static_cast<int>(video_frame->width);
    const int decoder_height =
        needs_resize ? media::kVideoDefaultHeight : static_cast<int>(video_frame->height);
    const bool stream_restarted = stream_index_ != video_frame->stream_idx;

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
            LOG_INFO("{} reused decoder across stream restart: stream={} codec={} size={}x{} count={}", name_,
                     stream_index_, video_frame->codec_type, decoder_width, decoder_height,
                     decoder_stream_reuse_count_);
        }
        return;
    }

    // Rebuild decoder on an incompatible stream restart or exception.
    if (stream_restarted || codec_reset_sign_) {
        if (decoder_->IsOpened()) {
            decoder_->Close();
            LOG_INFO("{} Decoder Reset Last stream:{} New Stream:{} SuccessCount:{} codecResetSign:{} ",
                     name_, stream_index_, video_frame->stream_idx, decode_count_, codec_reset_sign_);
        }
    }
    if (!decoder_->IsOpened()) {
        frame_info_.clear();
        stream_index_               = video_frame->stream_idx;
        codec_reset_sign_           = false;
        decode_count_               = 0;
        frame_index_                = -1;
        decoder_stream_reuse_count_ = 0;
        LOG_INFO("{} streamIndex:{} videoType:{} Changed, dumux video width:{}, height:{}", name_,
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
        LOG_INFO("{} Empty Data", name_);
        action_status_ = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!media::IsValidVideoResolution(static_cast<int>(video_frame->GetWidth()),
                                       static_cast<int>(video_frame->GetHeight()))) {
        action_status_ = util::ErrorEnum::VideoResolutionNotSupport;
        return;
    }

    // Preserve the reference chain while the channel runs, including viewer
    // construction and the short stop grace. Skipping P frames in that window
    // would make a late decoded-frame viewer wait for the next source IDR.
    // Sampling and host-copy avoidance happen after decoding, not at its input.
    if (!ValidateFrame(video_frame, false)) {
        return;
    }

    PrepareDecoder(video_frame);

    duration_stat_.BeginSample();
    bool is_decode_ret = false;
    FrameInfoSave(video_frame);
    media::DecodedVideoFrame decoded_frame;
#ifdef TEST_NO_DECODER
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
            decoded_frame = decoder_->DecodeFrame(video_frame->data.data(), video_frame->data.size(),
                                                  video_frame->index, is_decode_ret);
        } catch (const std::exception& e) {
            codec_reset_sign_ = true;
            LOG_ERRO("{} Last Frame is {} Have Decord Errors: {}", name_, frame_index_, e.what());
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
                LOG_WARN("{} {} consecutive decode failures (frameIndex:{}), forcing decoder reset", name_,
                         consecutive_decode_failures_, video_frame->index);
                codec_reset_sign_            = true;
                consecutive_decode_failures_ = 0;
            }
            action_status_ = util::ErrorEnum::DecoderFrameFailed;
            LOG_INFO("{} Decode Failed At {}, Last Frame:{}, consecutiveFails:{}", name_, video_frame->index,
                     frame_index_, consecutive_decode_failures_);
        }
        return;
    }

    const auto decoded_frame_index = decoded_frame.GetFrameIndex();
    const auto frame_info          = FrameInfoGet(static_cast<int64_t>(decoded_frame_index));
    const bool matched_frame_info =
        frame_info.index >= 0 && static_cast<uint64_t>(frame_info.index) == decoded_frame_index;
    const int64_t output_timestamp    = matched_frame_info ? frame_info.timestamp : util::GetMilliseconds();
    const int64_t output_stream_index = matched_frame_info ? frame_info.streamIndex : video_frame->stream_idx;

    AlgFrameDistributionPlan task_plan;
    const bool prepared_task_distribution = decoded_frame.IsDeferred();
    if (prepared_task_distribution) {
        task_plan = PrepareFrameDistribution(demux_data);
    }

    media::NativeVideoBufferPtr native_inference_buffer;
    if (task_plan.SupportsNativeInference() && NativeInferenceBufferEnabled()) {
        native_inference_buffer = decoded_frame.ExportNativeBuffer();
    }

    ViewerDistributionPlan viewer_plan;
#ifdef COSMO_MEDIA_USE_ROCKCHIP_BACKEND
    // Rockchip viewers move their existing FPS filter ahead of Copy-out. The
    // callback also rejects a saturated preview queue before host allocation.
    viewer_plan                                 = PrepareViewerDistribution();
    constexpr bool prepared_viewer_distribution = true;
#else
    constexpr bool prepared_viewer_distribution = false;
    bool has_viewer                             = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        has_viewer = !viewer_queue_.empty();
    }
#endif

    // MPP preserves deferred output selection. CPU/Sophon have already
    // materialized a backend frame here (Sophon uses device I420 memory).
    // Discard unused output before resize/distribution; this does not avoid
    // Sophon's decoder-output allocation and storage conversion.
#ifdef COSMO_MEDIA_USE_ROCKCHIP_BACKEND
    const bool host_frame_required = !decoded_frame.IsDeferred() || !task_plan.Empty() ||
                                     !viewer_plan.empty() || NeedsHostFrame(output_stream_index);
#else
    const bool host_frame_required = GetTaskCount() > 0 || has_viewer || NeedsHostFrame(output_stream_index);
#endif
    if (!host_frame_required) {
        decoded_frame.Discard();
        duration_stat_.EndSample();
        frame_index_ = video_frame->index;
        decode_count_ += 1;
        consecutive_decode_failures_ = 0;
        action_status_               = util::ErrorEnum::Success;
        return;
    }

    auto frame_data = decoded_frame.Materialize();
    duration_stat_.EndSample();
    if (!frame_data || !frame_data->Active()) {
        action_status_ = util::ErrorEnum::DecoderFrameFailed;
        LOG_WARN("{} decoded frame materialization failed at frame:{} stream:{}", name_, video_frame->index,
                 video_frame->stream_idx);
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
            LOG_WARN("{} Resize failed at frame:{} stream:{} (src:{}x{} dst:{}x{})", name_,
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

void AlgChannelDecode::FrameInfoSave(VideoPacketPtr packet) {
    if (!packet) {
        return;
    }
    AlgFrameInfo info;
    info.streamIndex = packet->stream_idx;
    info.index       = packet->index;
    info.timestamp   = packet->timestamp;
    frame_info_.push_back(info);
}

AlgFrameInfo AlgChannelDecode::FrameInfoGet(int64_t index) {
    AlgFrameInfo info;
    auto it = std::find_if(frame_info_.begin(), frame_info_.end(),
                           [&](const AlgFrameInfo& info_el) { return index == info_el.index; });

    // Check if the element was found.
    if (it != frame_info_.end()) {
        info.index       = index;
        info.timestamp   = it->timestamp;
        info.streamIndex = it->streamIndex;
        frame_info_.erase(it);
    }

    while (frame_info_.size() > 300) {
        frame_info_.pop_front();
    }

    return info;
}

// Image capture and viewer distribution — moved to AlgChannelDecodeCapture.cc

AlgDataPtr AlgChannelDecode::ColorConvert(AlgDataPtr demux_data, VideoFramePtr in_data,
                                          media::NativeVideoBufferPtr native_buffer) {
    if (!VideoFrameValid(in_data, true)) {
        return nullptr;
    }

    const auto convert_started = std::chrono::steady_clock::now();
    const auto record_duration = [&]() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - convert_started)
                                 .count();
        nn::GetInferencePipelineMetrics().RecordColorConvert(static_cast<uint64_t>(elapsed));
    };
    auto& transform = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
    VideoFramePtr ai_frame;
    const auto pixel_format = in_data->GetPixelFormat();
    if (pixel_format == media::PixelFormat::PIXEL_BGR8) {
        ai_frame = in_data;
    } else if (pixel_format == media::PixelFormat::PIXEL_RGB8) {
        auto i420_frame = transform.RGB2I420(in_data);
        if (i420_frame) {
            ai_frame = transform.I4202BGR(i420_frame);
        }
    } else if (pixel_format == media::PixelFormat::PIXEL_I420) {
        ai_frame = transform.I4202BGR(in_data);
    } else {
        LOG_WARN("{} unsupported decoded pixel format {}", name_, static_cast<int>(pixel_format));
    }
    if ((!ai_frame) || (!ai_frame->Active())) {
        record_duration();
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
    record_duration();
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
    if (decoder_) {
        decoder_->Close();
        decoder_.reset();
    }
#endif

    LOG_INFO("{} THREAD [{}] Stop ", name_, Name());
}
}  // namespace cosmo
