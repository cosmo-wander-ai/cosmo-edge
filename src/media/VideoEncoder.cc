// VideoEncoder — Video Encoder implementation.

#include "media/VideoEncoder.h"

#include "util/Log.h"

namespace cosmo {
namespace media {

    VideoEncoder::VideoEncoder() {}

    VideoEncoder::~VideoEncoder() {}

    size_t VideoEncoder::GetWidth() const {
        return width_;
    }

    size_t VideoEncoder::GetHeight() const {
        return height_;
    }

    void VideoEncoder::Set(media::VideoCodecType type, size_t w, size_t h) {
        codec_type_ = type;
        width_      = w;
        height_     = h;
    }

    VideoPacketPtr VideoEncoder::Encode(VideoFramePtr frame) {
        if (!VideoFrameValid(frame)) {
            LOG_WARN("{}", "invalid frame");
            return nullptr;
        }

        auto data = frame->GetData();
        auto pkt  = SendYUVFrame(data);
        if (!pkt) {
            LOG_WARN("{}", "send frame failed");
            return nullptr;
        }

        pkt->width      = frame->GetWidth();
        pkt->height     = frame->GetHeight();
        pkt->codec_type = codec_type_;
        pkt->timestamp  = frame->GetTimestamp();
        pkt->index      = static_cast<int64_t>(frame->GetFrameIndex());
        pkt->stream_idx = frame->GetStreamIndex();
        return pkt;
    }

}  // namespace media

}  // namespace cosmo