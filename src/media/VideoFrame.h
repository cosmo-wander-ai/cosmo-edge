#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "media/PixelFormat.h"
#include "mem/Block.h"
#include "mem/FixedBlockPool.h"

namespace cosmo {
namespace media {

    class VideoFrame {
    public:
        VideoFrame(int width, int height, PixelFormat format = PixelFormat::PIXEL_I420,
                   uint64_t frameIndex = 0, int64_t timestamp = 0);

        VideoFrame& operator=(VideoFrame&& data) noexcept;

        // Non-copyable: host_frame_data (malloc) and block (memory pool) use
        // raw owning pointers.  A shallow copy would cause double-free.
        VideoFrame(const VideoFrame&)            = delete;
        VideoFrame& operator=(const VideoFrame&) = delete;

        ~VideoFrame();

        size_t GetSize() const;

        size_t GetWidth() const;
        void SetWidth(size_t width);

        size_t GetHeight() const;
        void SetHeight(size_t height);

        PixelFormat GetPixelFormat() const;
        void SetFramePixelFormat(PixelFormat fmt);

        size_t GetChannel() const;

        uint64_t GetFrameIndex() const;
        void SetFrameIndex(uint64_t sequence);

        int64_t GetTimestamp() const;
        void SetTimestamp(int64_t timestamp);

        int64_t GetStreamIndex() const;
        void SetStreamIndex(int64_t streamIndex);

        bool Active() const;

        std::string GetName() const;

        uint8_t* GetData();

        uint8_t* GetHostData();
        void SetHostData(uint8_t* data);

    private:
        void Clear();

    private:
        bool active_    = false;
        size_t size_    = 0;
        size_t width_   = 0;
        size_t height_  = 0;
        size_t channel_ = 3;

        std::string uuid_;

        PixelFormat pixel_fmt_{PixelFormat::PIXEL_I420};

        uint64_t frame_index_ = 0;
        int64_t timestamp_    = 0;
        int64_t stream_index_ = -1;

        mem::Block* block_        = nullptr;
        uint8_t* host_frame_data_ = nullptr;
    };

}  // namespace media
}  // namespace cosmo

namespace cosmo::media {
using VideoFramePtr = std::shared_ptr<VideoFrame>;
}  // namespace cosmo::media

// Legacy global alias — kept for backward compatibility.
using VideoFramePtr = std::shared_ptr<cosmo::media::VideoFrame>;

// Legacy global function — kept for backward compatibility.
bool VideoFrameValid(VideoFramePtr frame, bool blog = false);
