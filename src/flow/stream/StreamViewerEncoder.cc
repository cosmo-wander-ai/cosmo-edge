// StreamViewerEncoder — Stream Viewer Encoder implementation.

#include "flow/stream/StreamViewerEncoder.h"

#include "media/PixelFormat.h"
#include "media/PreviewPipelineMetrics.h"
#include "mem/IDeviceContext.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameTransform.h"
#include "util/Log.h"

namespace cosmo {
StreamViewerEncoder::~StreamViewerEncoder() {
    async_queue_.Stop();
    if (encoder_) {
        encoder_.reset();
        encoder_ = nullptr;
    }
    LOG_INFO("{}", "Encoder Closed");
}

StreamViewerEncoder::StreamViewerEncoder(media::VideoCodecType videoType, int width, int height,
                                         RtmpStreamPusherPtr videoPusher, uint64_t timedPreviewToken)
    : video_type_(videoType),
      width_(width),
      height_(height),
      video_pusher_(videoPusher),
      timed_preview_token_(timedPreviewToken),
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

    return true;
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
        const auto encodeStart       = std::chrono::steady_clock::now();
        auto packet                  = encoder_->Encode(frame);
        const auto encodeNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now() - encodeStart)
                                           .count();
        duration_stat_.EndSample();
        media::GetPreviewPipelineMetrics().RecordTimedEncode(timed_preview_token_,
                                                             static_cast<uint64_t>(encodeNanoseconds));
        if (!packet) {
            return;
        }
        if (video_pusher_) {
            debug_info_.sendFrames += 1;
            video_pusher_->PushFrame(packet->GetData(), packet->GetSize());
        }
    }
    return;
}
}  // namespace cosmo
