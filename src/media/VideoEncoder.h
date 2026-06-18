#pragma once

#include <memory>

#include "media/VideoFrame.h"
#include "media/VideoPacket.h"

namespace cosmo {
namespace media {

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

        size_t GetWidth() const;
        size_t GetHeight() const;

        /// Factory — creates the correct backend encoder (Sophon or CPU).
        /// @param mediaHandle  Device handle (used by Sophon, ignored by CPU)
        static std::shared_ptr<VideoEncoder> Create(void* mediaHandle);

    protected:
        VideoCodecType codec_type_{VideoCodecType::kInvalid};

        size_t width_  = 1920;
        size_t height_ = 1080;
    };

}  // namespace media

}  // namespace cosmo
