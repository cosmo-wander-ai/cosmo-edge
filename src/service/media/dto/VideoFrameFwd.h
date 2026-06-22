// Forward declaration for VideoFramePtr — avoids pulling in media/VideoFrame.h
// (which transitively includes mem/Block.h, mem/FixedBlockPool.h) into service interfaces.

#pragma once

#include <memory>

namespace cosmo {
namespace media {
    class VideoFrame;
}  // namespace media
}  // namespace cosmo

namespace cosmo::media {
using VideoFramePtr = std::shared_ptr<VideoFrame>;
}  // namespace cosmo::media

// Legacy global alias — kept for backward compatibility.
using VideoFramePtr = std::shared_ptr<cosmo::media::VideoFrame>;
