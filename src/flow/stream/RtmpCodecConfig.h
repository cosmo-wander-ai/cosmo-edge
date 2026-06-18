#pragma once

#include <cstdint>
#include <vector>

#include "media/VideoCodecType.h"
#include "media/VideoUtil.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
}

namespace cosmo {

/// Codec configuration and NALU parameter extraction for RTMP output.
/// Parses SPS/PPS/VPS/SEI from raw NALU streams and configures
/// AVCodecParameters for the output format context.
class RtmpCodecConfig {
public:
    explicit RtmpCodecConfig(media::VideoCodecType codec_type, int width, int height, float fps);

    /// Parse NALU stream and extract VPS/SPS/PPS/SEI on first encounter.
    /// For I-frames missing SPS/VPS prefix, the data is prepended with cached parameters.
    /// @param data       raw NALU stream data
    /// @param size       data size in bytes
    /// @param[out] out_data  pointer to effective data (may point to internal prepend buffer)
    /// @param[out] out_size  effective data size
    /// @param[out] main_type  main frame type (I/P) at end of NALU packet
    /// @param[out] lead_type  first NALU type in the packet
    /// @return true when the packet contains an actual video slice (I/P), false for parameter-only packets.
    [[nodiscard]] bool ParseAndPrepare(const uint8_t* data, size_t size, const uint8_t*& out_data,
                                       size_t& out_size, media::HFrameType& main_type,
                                       media::HFrameType& lead_type);

    /// Configure AVStream codecpar and time_base from extracted parameters.
    /// Must be called after the first I-frame with SPS/PPS has been extracted.
    void ConfigureStream(AVStream* stream) const;

    /// Map VideoCodecType to FFmpeg AVCodecID.
    [[nodiscard]] static AVCodecID ToAvCodecId(media::VideoCodecType type);

    /// Estimate bitrate based on resolution.
    [[nodiscard]] static int64_t EstimateBitrate(int width, int height);

    [[nodiscard]] bool HasParameters() const {
        if (codec_type_ == media::VideoCodecType::kH265) {
            return !vps_.empty() && !sps_.empty() && !pps_.empty();
        }
        return !sps_.empty() && !pps_.empty();
    }

private:
    media::VideoCodecType codec_type_;
    int width_;
    int height_;
    float fps_;

    std::vector<uint8_t> vps_;
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    std::vector<uint8_t> sei_;
    std::vector<uint8_t> prepend_buffer_;  // reusable buffer for I-frame parameter prepend
};

}  // namespace cosmo
