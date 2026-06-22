// AlgChannelDecode — video decoding, color conversion and frame distribution.
// Image capture and viewer distribution are in AlgChannelDecodeCapture.cc.

#include <filesystem>

#include "flow/channel/AlgChannel.h"
#include "media/VideoFrame.h"
#include "mem/IDeviceContext.h"
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
    // Rebuild decoder on stream restart or exception.
    if ((stream_index_ != video_frame->stream_idx) || (codec_reset_sign_)) {
        if (decoder_->IsOpened()) {
            decoder_->Close();
            LOG_INFO("{} Decoder Reset Last stream:{} New Stream:{} SuccessCount:{} codecResetSign:{} ",
                     name_, stream_index_, video_frame->stream_idx, decode_count_, codec_reset_sign_);
        }
    }
    if (!decoder_->IsOpened()) {
        frame_info_.clear();
        stream_index_     = video_frame->stream_idx;
        codec_reset_sign_ = false;
        decode_count_     = 0;
        frame_index_      = -1;
        LOG_INFO("{} streamIndex:{} videoType:{} Changed, dumux video width:{}, height:{}", name_,
                 stream_index_, video_frame->codec_type, video_frame->width, video_frame->height);

        if (NeedsResize(video_frame)) {
            decoder_->SetCodecType(video_frame->codec_type, media::kVideoDefaultWidth,
                                   media::kVideoDefaultHeight);
        } else {
            decoder_->SetCodecType(video_frame->codec_type, static_cast<int>(video_frame->width),
                                   static_cast<int>(video_frame->height));
        }
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

    bool just_need_i_frame = (GetMaxFps() < 1.0f) && (viewer_queue_.empty());

    if (!ValidateFrame(video_frame, just_need_i_frame)) {
        return;
    }

    // When inference is slower than input, backlog quickly amplifies VRAM usage in decode/convert.
    // Keep keyframes and drop part of non-I frames to stabilize memory.
    // if ((dec_queue_->RestSize() >= kDecodeBacklogDropThreshold) && (!video_frame->IsIFrame())) {
    //     dec_queue_->RecordDiscard();
    // }

    PrepareDecoder(video_frame);

    if (just_need_i_frame) {
        if (!video_frame->IsIFrame()) {
            return;
        }
        if (0 == decode_count_ % 300) {
            LOG_INFO("{} MaxFps:{} Just Need I Frame, Frame:{} is {} Frame", name_, GetMaxFps(),
                     video_frame->GetSequence(), video_frame->IsIFrame() ? "I" : "P");
        }
    }

    duration_stat_.BeginSample();
    bool is_decode_ret = false;
    FrameInfoSave(video_frame);
    VideoFramePtr frame_data = nullptr;
#ifdef TEST_NO_DECODER
    frame_data = std::make_shared<media::VideoFrame>(1920, 1080);
#else
    if (video_frame->codec_type == media::VideoCodecType::kMjpeg) {
        frame_data = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>().DecodeJpeg(
            std::vector<u_int8_t>(video_frame->data.begin(), video_frame->data.end()));
        if (frame_data) {
            // MJPEG hardware decode returns frames without business-side frame
            // indices; fill in index/stream/timestamp for overlay alignment.
            frame_data->SetFrameIndex(static_cast<uint64_t>(video_frame->index));
            frame_data->SetStreamIndex(video_frame->stream_idx);
            frame_data->SetTimestamp(video_frame->timestamp);
        }
        is_decode_ret = (frame_data != nullptr);
    } else {
        try {
            frame_data = decoder_->Decode(video_frame->data.data(), video_frame->data.size(),
                                          video_frame->index, is_decode_ret);
        } catch (const std::exception& e) {
            codec_reset_sign_ = true;
            LOG_ERRO("{} Last Frame is {} Have Decord Errors: {}", name_, frame_index_, e.what());
            return;
        }
    }
#endif
    duration_stat_.EndSample();

    if (frame_data) {
        auto frame_info = FrameInfoGet(static_cast<int64_t>(frame_data->GetFrameIndex()));
        if (static_cast<uint64_t>(frame_info.index) == frame_data->GetFrameIndex()) {
            frame_data->SetTimestamp(frame_info.timestamp);
            frame_data->SetStreamIndex(frame_info.streamIndex);
        } else {
            frame_data->SetTimestamp(util::GetMilliseconds());
        }
    } else {
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

    DistributeViewer(output_frame);
    DoCaptureImage(output_frame);
    CaptureJpeg(output_frame);
    DistributorData(demux_data, output_frame,
                    [this](AlgDataPtr frame, VideoFramePtr in_data) { return ColorConvert(frame, in_data); });
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

AlgDataPtr AlgChannelDecode::ColorConvert(AlgDataPtr demux_data, VideoFramePtr in_data) {
    if (!VideoFrameValid(in_data, true)) {
        return nullptr;
    }

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
        action_status_ = util::ErrorEnum::DecoderColorConvertFailed;
        return nullptr;
    }

    ai_frame->SetFrameIndex(in_data->GetFrameIndex());
    ai_frame->SetTimestamp(in_data->GetTimestamp());
    ai_frame->SetStreamIndex(in_data->GetStreamIndex());

    AlgDataPtr data           = std::make_shared<AlgData>();
    data->chanDataOrig.packet = demux_data->chanDataOrig.packet;
    data->chanDataOrig.fps    = demux_data->chanDataOrig.fps;
    data->dataType            = AlgDataType::ChannelDataDec;
    data->chanDataDec.frame   = ai_frame;
    data->channelId           = channel_id_;

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
    if (decoder_) {
        decoder_->Close();
        decoder_.reset();
    }
#endif

    LOG_INFO("{} THREAD [{}] Stop ", name_, Name());
}
}  // namespace cosmo
