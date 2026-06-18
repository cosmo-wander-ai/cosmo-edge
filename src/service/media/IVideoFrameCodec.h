/// @file IVideoFrameCodec.h
/// @brief JPEG encode/decode interface for VideoFrame objects.
///        ISP split from IVideoFrameService.
///        Consumed by AlgChannelDecode, PTaskBaseUpload.
#pragma once

#include <vector>

#include "service/media/dto/VideoFrameFwd.h"

namespace cosmo::service {

/// JPEG codec operations for VideoFrame objects.
class IVideoFrameCodec {
public:
    virtual ~IVideoFrameCodec() = default;

    /// Encode a VideoFrame to JPEG format.
    /// @param srcPicture Source frame.
    /// @return JPEG-encoded byte buffer.
    virtual std::vector<u_char> EncodeJpeg(const VideoFramePtr srcPicture) = 0;

    /// Decode a JPEG byte buffer into a VideoFrame.
    /// @param data JPEG-encoded byte buffer.
    /// @return Decoded video frame.
    virtual VideoFramePtr DecodeJpeg(const std::vector<u_int8_t>& data) = 0;
};

}  // namespace cosmo::service
