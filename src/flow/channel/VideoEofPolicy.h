// Local-video EOF lifecycle policy shared by the demuxer and monitor guard.

#pragma once

namespace cosmo::flow {

enum class VideoEofDisposition {
    Reopen,
    Complete,
};

// video_read_count is the number of successful opens, including the current
// playback. A non-positive repeat count means infinite playback.
constexpr VideoEofDisposition DecideVideoEof(bool is_live_stream, int video_read_count,
                                             int video_repeat_count) {
    if (is_live_stream || video_repeat_count <= 0 || video_read_count < video_repeat_count) {
        return VideoEofDisposition::Reopen;
    }
    return VideoEofDisposition::Complete;
}

// Defense for the camera monitor: a transient ReadEnd can never be terminal
// while the demuxer has already committed to reopening the stream.
constexpr bool IsTerminalOfflineReadEnd(bool is_read_end, bool repeat_pending) {
    return is_read_end && !repeat_pending;
}

}  // namespace cosmo::flow
