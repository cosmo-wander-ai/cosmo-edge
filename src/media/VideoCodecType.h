#pragma once

namespace cosmo::media {
enum class VideoCodecType {
    kInvalid = 0,  // Invalid
    kH264,         // H264
    kH265,         // H265
    kMjpeg,        // MJPEG (USB cameras, consistent with old eb6daaa6)
};
}  // namespace cosmo::media
