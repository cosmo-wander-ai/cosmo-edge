#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#ifdef __cplusplus
}
#endif

#include "media/VideoEncoder.h"

namespace cosmo {
namespace media {

    /// CPU-backend video encoder — uses FFmpeg software codec (libavcodec).
    /// Provides the same interface as VideoEncoderSophon but without
    /// any hardware device dependency.
    class VideoEncoderCpu : public VideoEncoder {
    public:
        explicit VideoEncoderCpu();

        ~VideoEncoderCpu() override;

        bool Open() override;

        VideoPacketPtr SendYUVFrame(void* data) override;

    private:
        void Clean();

        AVCodecContext* codec_ctx_{nullptr};
        AVFrame* av_frame_{nullptr};
        AVPacket* av_packet_{nullptr};

        int64_t frame_pts_{0};
        bool closed_{true};
    };

}  // namespace media
}  // namespace cosmo
