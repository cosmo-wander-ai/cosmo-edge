// StreamViewerEncoder — Stream Viewer Encoder implementation.

#include "flow/stream/StreamViewerEncoder.h"

#include <algorithm>

#include "media/PixelFormat.h"
#include "media/VideoUtil.h"
#include "mem/IDeviceContext.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/Log.h"

namespace cosmo {
namespace {
    size_t MinStartupKeyFrameSize(int width, int height) {
        const auto pixels =
            static_cast<size_t>(std::max(width, 1)) * static_cast<size_t>(std::max(height, 1));
        return std::max<size_t>(1024, pixels / 4096);
    }
}  // namespace

StreamViewerEncoder::~StreamViewerEncoder() {
    async_queue_.Stop();
    if (encoder_) {
        encoder_.reset();
        encoder_ = nullptr;
    }
    LOG_INFO("{}", "Encoder Closed");
}

StreamViewerEncoder::StreamViewerEncoder(media::VideoCodecType videoType, int width, int height,
                                         RtmpStreamPusherPtr videoPusher)
    : video_type_(videoType),
      width_(width),
      height_(height),
      video_pusher_(videoPusher),
      async_queue_("StreamViewerEncoderQueue", 8),
      duration_stat_("Encode") {
    if (!OpenEncoder()) {
        return;
    }

    async_queue_.SetProcessor([this](VideoFramePtr&& data) { ProcFrame(std::move(data)); });
    LOG_INFO("{}", "Encoder Create Success");
}

bool StreamViewerEncoder::OpenEncoder() {
    auto* mediaHandle = service::ServiceRegistry::Instance().Get<mem::IDeviceContext>().GetMediaHandle();
    encoder_          = media::VideoEncoder::Create(mediaHandle);
    encoder_->Set(video_type_, width_, height_);
    if (!encoder_->Open()) {
        LOG_WARN("{}", "open encoder failed");
        encoder_.reset();
        encoder_ = nullptr;
        return false;
    }

    startup_small_key_frame_count_ = 0;
    startup_key_frame_accepted_    = false;
    return true;
}

bool StreamViewerEncoder::ContainsKeyFrame(const uint8_t* data, size_t size) const {
    if (!data || size == 0) {
        return false;
    }

    size_t offset = 0;
    while (offset < size) {
        const size_t nal_size = media::SeparateHVideoFrame(data + offset, size - offset);
        if (nal_size == 0) {
            break;
        }
        if (media::GetFrameType(video_type_, data + offset) == media::HFrameType::I) {
            return true;
        }
        offset += nal_size;
    }

    return false;
}

bool StreamViewerEncoder::IsSmallStartupKeyFrame(const uint8_t* data, size_t size) const {
    if (startup_key_frame_accepted_) {
        return false;
    }

    return ContainsKeyFrame(data, size) && size < MinStartupKeyFrameSize(width_, height_);
}

bool StreamViewerEncoder::HandFrame(VideoFramePtr frame) {
    if (encoder_) {
        debug_info_.recvFrames += 1;
        // Viewer prioritizes freshness: drop old queued frames when overloaded.
        return async_queue_.Insert(frame, true);
    }
    return false;
}

void StreamViewerEncoder::ProcFrame(VideoFramePtr frame) {
    if (encoder_) {
        if (frame && frame->GetPixelFormat() != media::PixelFormat::PIXEL_I420) {
            auto& transform = service::ServiceRegistry::Instance().Get<service::IVideoFrameTransform>();
            if (frame->GetPixelFormat() == media::PixelFormat::PIXEL_BGR8) {
                frame = transform.BGR2I420(frame);
            } else if (frame->GetPixelFormat() == media::PixelFormat::PIXEL_RGB8) {
                frame = transform.RGB2I420(frame);
            } else {
                LOG_WARN("StreamViewerEncoder unsupported frame pixel format {}",
                         static_cast<int>(frame->GetPixelFormat()));
                return;
            }
            if (!VideoFrameValid(frame)) {
                LOG_WARN("{}", "StreamViewerEncoder convert frame to I420 failed");
                return;
            }
        }
        duration_stat_.BeginSample();
        auto packet = encoder_->Encode(frame);
        duration_stat_.EndSample();
        if (!packet) {
            return;
        }
        if (IsSmallStartupKeyFrame(packet->GetData(), packet->GetSize())) {
            startup_small_key_frame_count_ += 1;
            LOG_WARN("Drop small startup keyframe size:{} min:{} count:{}", packet->GetSize(),
                     MinStartupKeyFrameSize(width_, height_), startup_small_key_frame_count_);
            LOG_WARN("Reopen encoder after small startup keyframe, width:{} height:{}", width_, height_);
            encoder_.reset();
            OpenEncoder();
            return;
        }
        if (packet && video_pusher_) {
            if (ContainsKeyFrame(packet->GetData(), packet->GetSize())) {
                startup_key_frame_accepted_ = true;
            }
            startup_small_key_frame_count_ = 0;
            debug_info_.sendFrames += 1;
            video_pusher_->PushFrame(packet->GetData(), packet->GetSize());
        }
    }
    return;
}
}  // namespace cosmo
