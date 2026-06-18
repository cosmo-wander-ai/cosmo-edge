#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>

#include "media/VideoCodecType.h"

namespace cosmo {
namespace media {

    enum class HFrameType { I, P, VPS, SPS, PPS, SEI, AUD, UNKNOWN };

    HFrameType GetFrameType(VideoCodecType codec, const uint8_t* data);

    // Determine frame type (only check the first few bytes)
    HFrameType GetH264FrameType(const uint8_t* data);

    // Determine frame type (only check the first few bytes)
    HFrameType GetH265FrameType(const uint8_t* data);

    // Separate NALU for h.264, h.265
    size_t SeparateHVideoFrame(const uint8_t* data, size_t size);

    const uint8_t* RemoveHFrameSeparator(const uint8_t* data, size_t& removedSize);

}  // namespace media
}  // namespace cosmo
