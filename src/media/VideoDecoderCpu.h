#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#ifdef __cplusplus
}
#endif

#include "media/VideoDecoder.h"

namespace cosmo {
namespace media {

    /// CPU-backend video decoder — uses FFmpeg software codec (libavcodec).
    /// Provides the same interface as VideoDecoderSophon but without
    /// any hardware device dependency.
    class VideoDecoderCpu : public VideoDecoder {
    public:
        VideoDecoderCpu(size_t name);

        ~VideoDecoderCpu() override;

        bool Open() override;
        bool Close() override;

        bool IsOpened() override;

        bool SendPacket(const uint8_t* pkt, size_t len, int64_t frame_idx) override;

        VideoFramePtr GetFrame() override;

    private:
        /// Copy decoded AVFrame (YUV420P) data into a VideoFrame's host-memory Block.
        bool CopyAVFrameToVideoFrame(AVFrame* src, VideoFramePtr dst);

        AVCodecContext* codec_ctx_{nullptr};
        AVFrame* av_frame_{nullptr};
        AVPacket* av_packet_{nullptr};

        bool opened_{false};
    };

}  // namespace media
}  // namespace cosmo
