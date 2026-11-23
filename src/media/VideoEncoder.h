#pragma once

#include <memory>
#include <string>

#include "media/VideoFrame.h"
#include "media/VideoPacket.h"

namespace cosmo {
namespace media {

    struct VideoEncoderCapability {
        bool available{false};
        std::string backend;
        std::string implementation;
        std::string detail;
    };

    class VideoEncoder {
    public:
        explicit VideoEncoder();

        virtual ~VideoEncoder();

        /**
         * MUST be called before @ref Open()
         */
        void Set(VideoCodecType type, size_t width, size_t height);

        virtual bool Open() = 0;

        VideoPacketPtr Encode(VideoFramePtr frame);

        virtual VideoPacketPtr SendYUVFrame(void*) = 0;

        /// A permanent failure requires the owning preview/publisher to be recreated.
        virtual bool IsPermanentlyFaulted() const noexcept {
            return false;
        }

        size_t GetWidth() const;
        size_t GetHeight() const;

        /// Factory — creates the selected platform backend encoder.
        /// @param mediaHandle Device handle (used by hardware backends, ignored by CPU)
        static std::shared_ptr<VideoEncoder> Create(void* mediaHandle);

        /// Reports the deterministic implementation that Create/Open will use.
        /// A missing approved implementation is reported as unavailable instead
        /// of falling through to an arbitrary FFmpeg hardware encoder.
        static VideoEncoderCapability Probe(VideoCodecType type);

    protected:
        VideoCodecType codec_type_{VideoCodecType::kInvalid};

        size_t width_  = 1920;
        size_t height_ = 1080;
    };

}  // namespace media

}  // namespace cosmo
