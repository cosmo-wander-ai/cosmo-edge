#include "media/VideoPacketColorWitness.h"

#include <algorithm>

extern "C" {
#include "libavutil/mem.h"
#include "libavutil/sha.h"
}

namespace cosmo::media {
namespace {
    bool Digest(const uint8_t* data, size_t size, std::array<uint8_t, 32>& out) {
        if (!data || size == 0)
            return false;
        AVSHA* ctx = av_sha_alloc();
        if (!ctx)
            return false;
        const bool ok = av_sha_init(ctx, 256) == 0;
        if (ok) {
            // Older FFmpeg accepts unsigned-int lengths; use bounded chunks.
            constexpr size_t kChunk = 1024 * 1024;
            for (size_t offset = 0; offset < size;) {
                const size_t count = std::min(kChunk, size - offset);
                av_sha_update(ctx, data + offset, static_cast<unsigned int>(count));
                offset += count;
            }
            av_sha_final(ctx, out.data());
        }
        av_free(ctx);
        return ok;
    }
}  // namespace

VideoPacketColorWitness::VideoPacketColorWitness(const VideoPacket& packet,
                                                 const std::array<uint8_t, 32>& digest)
    : digest_(digest),
      size_(packet.data.size()),
      pts_(packet.pts),
      stream_(packet.stream_idx),
      index_(packet.index),
      timestamp_(packet.timestamp),
      timestamp_epoch_(packet.timestamp_epoch),
      presentation_time_valid_(packet.presentation_time_valid),
      presentation_timestamp_ms_(packet.presentation_timestamp_ms),
      presentation_steady_ms_(packet.presentation_steady_ms),
      width_(packet.width),
      height_(packet.height),
      codec_(packet.codec_type),
      correlated_(packet.color_packet_pts_correlated),
      parameter_change_(packet.color_parameter_change),
      container_present_(packet.color_container_present),
      container_space_(packet.container_color_space),
      container_range_(packet.container_color_range) {}

std::shared_ptr<const VideoPacketColorWitness> VideoPacketColorWitness::Create(const VideoPacket& packet) {
    std::array<uint8_t, 32> digest{};
    if (!Digest(packet.data.data(), packet.data.size(), digest))
        return {};
    return std::shared_ptr<const VideoPacketColorWitness>(new VideoPacketColorWitness(packet, digest));
}

bool VideoPacketColorWitness::Matches(const VideoPacket& packet, const uint8_t* retained, size_t size) const {
    if (size != size_ || packet.data.size() != size_ || packet.pts != pts_ || packet.stream_idx != stream_ ||
        packet.index != index_ || packet.timestamp != timestamp_ ||
        packet.timestamp_epoch != timestamp_epoch_ ||
        packet.presentation_time_valid != presentation_time_valid_ ||
        packet.presentation_timestamp_ms != presentation_timestamp_ms_ ||
        packet.presentation_steady_ms != presentation_steady_ms_ || packet.width != width_ ||
        packet.height != height_ || packet.codec_type != codec_ ||
        packet.color_packet_pts_correlated != correlated_ ||
        packet.color_parameter_change != parameter_change_ ||
        packet.color_container_present != container_present_ ||
        packet.container_color_space != container_space_ || packet.container_color_range != container_range_)
        return false;
    std::array<uint8_t, 32> digest{};
    return Digest(retained, size, digest) && digest == digest_;
}
}  // namespace cosmo::media
