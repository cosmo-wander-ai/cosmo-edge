#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "media/NativeVideoBuffer.h"
#include "media/VideoCodecType.h"

namespace cosmo::media {

class VideoPacketColorWitness;

struct VideoPacket {
    VideoPacket() = default;

    ~VideoPacket() = default;

    [[nodiscard]] int64_t GetSequence() const;

    [[nodiscard]] int64_t GetTimestamp() const;

    [[nodiscard]] int64_t GetTimestampEpoch() const;

    [[nodiscard]] uint8_t* GetData();
    [[nodiscard]] size_t GetSize() const;

    [[nodiscard]] VideoCodecType GetType() const;

    [[nodiscard]] size_t GetWidth() const;
    [[nodiscard]] size_t GetHeight() const;

    [[nodiscard]] float GetFPS() const;

    [[nodiscard]] bool IsIFrame() const;
    std::vector<uint8_t> data;
    int64_t pts             = 0;
    int64_t timestamp       = 0;
    int64_t timestamp_epoch = 0;
    // The original fields above remain packet-arrival clocks. These separate
    // fields are demux-derived presentation clocks, bound by color_witness.
    bool presentation_time_valid      = false;
    int64_t presentation_timestamp_ms = 0;
    int64_t presentation_steady_ms    = 0;

    // Container values are only conflict evidence, never sufficient by
    // themselves to attest color for a decoded frame.
    bool color_container_present                = false;
    NativeVideoColorSpace container_color_space = NativeVideoColorSpace::Unspecified;
    NativeVideoColorRange container_color_range = NativeVideoColorRange::Unspecified;
    bool color_parameter_change                 = false;
    bool color_packet_pts_correlated            = false;
    std::shared_ptr<const VideoPacketColorWitness> color_witness;

    std::chrono::steady_clock::time_point time_point;

    int64_t stream_idx = 0;

    int64_t index   = 0;
    int64_t abs_idx = 0;

    bool is_i_frame = false;

    VideoCodecType codec_type = VideoCodecType::kH264;

    size_t width  = 0;
    size_t height = 0;

    float fps = 0.f;
};

}  // namespace cosmo::media

namespace cosmo::media {
using VideoPacketPtr = std::shared_ptr<VideoPacket>;
}  // namespace cosmo::media

// Legacy global alias — kept for backward compatibility.
// TODO(refactor): Remove after downstream modules migrate to cosmo::media::VideoPacketPtr.
using VideoPacketPtr = std::shared_ptr<cosmo::media::VideoPacket>;
