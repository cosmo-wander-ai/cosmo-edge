// Finalize an MP4 presentation range without changing sample DTS or CTS.
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "mp4v2/mp4v2.h"

namespace cosmo::media {
inline bool FinalizeMp4Presentation(MP4FileHandle mp4_handle_, MP4TrackId track_id_,
                                    uint64_t decode_duration_, uint64_t presentation_start_,
                                    uint64_t presentation_end_) {
    if (presentation_end_ <= decode_duration_)
        return true;

    // This recorder creates a single video track. Preserve all sample DTS,
    // CTS offsets and the initial presentation time; the edit explicitly
    // covers [0, maximum presentation end] without lengthening any sample.
    const uint64_t movie_scale     = MP4GetTimeScale(mp4_handle_);
    constexpr uint64_t track_scale = 90000;
    // In version-0 duration atoms all ones denotes unknown duration.
    constexpr uint64_t max_field = std::numeric_limits<uint32_t>::max() - 1ULL;
    if (movie_scale == 0 || presentation_end_ > max_field || presentation_start_ >= presentation_end_)
        return false;  // These files use version-0, 32-bit duration atoms.
    const uint64_t whole     = presentation_end_ / track_scale;
    const uint64_t remainder = presentation_end_ % track_scale;
    if (whole > max_field / movie_scale)
        return false;
    const uint64_t fraction = (remainder * movie_scale + track_scale - 1) / track_scale;
    if (fraction > max_field - whole * movie_scale)
        return false;
    const uint64_t movie_end      = whole * movie_scale + fraction;
    const uint64_t media_duration = std::max(decode_duration_, presentation_end_ - presentation_start_);
    if (movie_end == 0 ||
        MP4AddTrackEdit(mp4_handle_, track_id_, MP4_INVALID_EDIT_ID, 0, movie_end, false) ==
            MP4_INVALID_EDIT_ID ||
        !MP4SetTrackIntegerProperty(mp4_handle_, track_id_, "mdia.mdhd.duration", media_duration) ||
        !MP4SetTrackIntegerProperty(mp4_handle_, track_id_, "tkhd.duration", movie_end) ||
        !MP4SetIntegerProperty(mp4_handle_, "moov.mvhd.duration", movie_end))
        return false;

    uint64_t media_read{}, track_read{}, movie_read{};
    return MP4GetTrackIntegerProperty(mp4_handle_, track_id_, "mdia.mdhd.duration", &media_read) &&
           MP4GetTrackIntegerProperty(mp4_handle_, track_id_, "tkhd.duration", &track_read) &&
           MP4GetIntegerProperty(mp4_handle_, "moov.mvhd.duration", &movie_read) &&
           media_read == media_duration && track_read == movie_end && movie_read == movie_end &&
           MP4GetTrackNumberOfEdits(mp4_handle_, track_id_) == 1 &&
           MP4GetTrackEditMediaStart(mp4_handle_, track_id_, 1) == 0 &&
           MP4GetTrackEditDuration(mp4_handle_, track_id_, 1) == movie_end;
}

}  // namespace cosmo::media
