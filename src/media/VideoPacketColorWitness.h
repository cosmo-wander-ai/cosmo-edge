#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "media/VideoPacket.h"

namespace cosmo::media {

class VideoDemuxer;

// Created by the demuxer after AU and timestamp handling, not by a decoder
// caller. The decoder checks the digest against the retained bytes it submits.
class VideoPacketColorWitness final {
public:
    [[nodiscard]] bool Matches(const VideoPacket& packet, const uint8_t* retained, size_t size) const;

private:
    friend class VideoDemuxer;
#ifdef COSMO_AXERA_COLOR_TEST_SEAM
    friend class VideoPacketWitnessTestAccess;
#endif
    static std::shared_ptr<const VideoPacketColorWitness> Create(const VideoPacket& packet);
    explicit VideoPacketColorWitness(const VideoPacket& packet, const std::array<uint8_t, 32>& digest);

    std::array<uint8_t, 32> digest_{};
    size_t size_{0};
    int64_t pts_{0}, stream_{0}, index_{0}, timestamp_{0}, timestamp_epoch_{0};
    bool presentation_time_valid_{false};
    int64_t presentation_timestamp_ms_{0}, presentation_steady_ms_{0};
    size_t width_{0}, height_{0};
    VideoCodecType codec_{VideoCodecType::kInvalid};
    bool correlated_{false}, parameter_change_{false}, container_present_{false};
    NativeVideoColorSpace container_space_{NativeVideoColorSpace::Unspecified};
    NativeVideoColorRange container_range_{NativeVideoColorRange::Unspecified};
};

}  // namespace cosmo::media
