#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

#include "media/NativeVideoBuffer.h"

namespace cosmo::media {

// Association only: this type never parses a bitstream and cannot itself
// establish that SPS evidence is trustworthy. The parser validates the active
// parameter sets, including explicitly absent VUI fields, before setting vui_verified.
struct VideoColorEvidence {
    NativeVideoColorSpace bitstream_space{NativeVideoColorSpace::Unspecified};
    NativeVideoColorRange bitstream_range{NativeVideoColorRange::Unspecified};
    uint32_t bitstream_width{0};
    uint32_t bitstream_height{0};
    bool vui_verified{false};
    bool idr{false};
    bool container_present{false};
    NativeVideoColorSpace container_space{NativeVideoColorSpace::Unspecified};
    NativeVideoColorRange container_range{NativeVideoColorRange::Unspecified};
};

enum class ColorMatrixOrigin { Bitstream, MediaMetadata, ApplicationDefault };

struct VideoColorAssociation {
    uint64_t generation{0};
    uint64_t decoder_token{0};
    int64_t source_pts{0};
    int64_t frame_index{-1};
    NativeVideoColorSpace color_space{NativeVideoColorSpace::Unspecified};
    NativeVideoColorRange color_range{NativeVideoColorRange::Unspecified};
    uint32_t bitstream_width{0};
    uint32_t bitstream_height{0};
    ColorMatrixOrigin matrix_origin{ColorMatrixOrigin::Bitstream};
};

// Decoder-owner-thread only. A monotonically increasing synthetic decoder PTS
// token associates each input AU with its output despite decode reordering.
// An unresolved color, ambiguous token, generation change, metadata conflict,
// or changed color poisons this generation; old outputs may still be pending.
// Only a strictly newer generation can reset the poison and pending entries.
class VideoColorProvenance {
public:
    static constexpr size_t kMaxPending = 128;

    bool BeginGeneration(uint64_t generation) {
        if (generation == 0 || generation <= generation_)
            return false;
        generation_       = generation;
        fault_            = false;
        last_token_       = 0;
        last_frame_index_ = -1;
        have_color_       = false;
        pending_.clear();
        return true;
    }

    bool Register(uint64_t generation, uint64_t decoder_token, int64_t frame_index, bool has_source_pts,
                  int64_t source_pts, const VideoColorEvidence& evidence) {
        if (generation != generation_ || generation_ == 0 || fault_)
            return false;
        auto resolved_space = evidence.bitstream_space;
        auto origin         = ColorMatrixOrigin::Bitstream;
        if (resolved_space == NativeVideoColorSpace::Unspecified) {
            if (evidence.container_present &&
                evidence.container_space != NativeVideoColorSpace::Unspecified) {
                resolved_space = evidence.container_space;
                origin         = ColorMatrixOrigin::MediaMetadata;
            } else {
                // Application default for the existing AX native 1080p model path.
                // This is an assumption, not a declaration in the source bitstream.
                resolved_space = NativeVideoColorSpace::Bt709;
                origin         = ColorMatrixOrigin::ApplicationDefault;
            }
        }
        const auto resolved_range = evidence.bitstream_range;
        const bool conflict =
            evidence.container_present && ((evidence.container_space != NativeVideoColorSpace::Unspecified &&
                                            evidence.container_space != resolved_space) ||
                                           (evidence.container_range != NativeVideoColorRange::Unspecified &&
                                            evidence.container_range != resolved_range));
        if (decoder_token == 0 || decoder_token <= last_token_ || frame_index < 0 ||
            frame_index <= last_frame_index_ || !has_source_pts ||
            source_pts == std::numeric_limits<int64_t>::min() || !evidence.vui_verified ||
            evidence.bitstream_width == 0 || evidence.bitstream_height == 0 ||
            !Known(resolved_space, resolved_range) || conflict ||
            (have_color_ && (space_ != resolved_space || range_ != resolved_range)) ||
            pending_.size() >= kMaxPending) {
            Poison();
            return false;
        }
        VideoColorAssociation value{generation,
                                    decoder_token,
                                    source_pts,
                                    frame_index,
                                    resolved_space,
                                    resolved_range,
                                    evidence.bitstream_width,
                                    evidence.bitstream_height,
                                    origin};
        pending_.emplace(decoder_token, value);
        last_token_       = decoder_token;
        last_frame_index_ = frame_index;
        space_            = resolved_space;
        range_            = resolved_range;
        last_origin_      = origin;
        have_color_       = true;
        return true;
    }

    bool Resolve(uint64_t generation, uint64_t decoder_token, int64_t decoded_frame_index,
                 VideoColorAssociation& output) {
        output = {};
        if (generation != generation_ || generation_ == 0 || fault_)
            return false;
        const auto item = pending_.find(decoder_token);
        if (item == pending_.end() || item->second.frame_index != decoded_frame_index) {
            Poison();
            return false;
        }
        output = item->second;
        pending_.erase(item);
        return true;
    }

    [[nodiscard]] ColorMatrixOrigin LastMatrixOrigin() const {
        return last_origin_;
    }
    [[nodiscard]] bool Faulted() const {
        return fault_;
    }
    [[nodiscard]] size_t Pending() const {
        return pending_.size();
    }

    // Submission or parser failures invalidate all pending associations.
    void Invalidate() {
        Poison();
    }

private:
    static bool Known(NativeVideoColorSpace space, NativeVideoColorRange range) {
        return (space == NativeVideoColorSpace::Bt601 || space == NativeVideoColorSpace::Bt709 ||
                space == NativeVideoColorSpace::Bt2020) &&
               (range == NativeVideoColorRange::Limited || range == NativeVideoColorRange::Full);
    }

    void Poison() {
        fault_ = true;
        pending_.clear();
    }

    uint64_t generation_{0};
    uint64_t last_token_{0};
    int64_t last_frame_index_{-1};
    bool fault_{false};
    bool have_color_{false};
    ColorMatrixOrigin last_origin_{ColorMatrixOrigin::Bitstream};
    NativeVideoColorSpace space_{NativeVideoColorSpace::Unspecified};
    NativeVideoColorRange range_{NativeVideoColorRange::Unspecified};
    std::map<uint64_t, VideoColorAssociation> pending_;
};

}  // namespace cosmo::media
