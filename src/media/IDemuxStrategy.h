/// @file IDemuxStrategy.h
/// @brief Strategy interface for source-type-specific demuxer behavior.
///        Each source type (RTSP, USB/V4L2, local file) implements this interface,
///        eliminating is_usb/is_network boolean branches from VideoDemuxer.
#pragma once

#include <string>

#include "media/VideoCodecType.h"
#include "util/ErrorCode.h"

struct AVFormatContext;

namespace cosmo::media {

class IDemuxStrategy {
public:
    virtual ~IDemuxStrategy() = default;

    IDemuxStrategy()                                 = default;
    IDemuxStrategy(const IDemuxStrategy&)            = delete;
    IDemuxStrategy& operator=(const IDemuxStrategy&) = delete;
    IDemuxStrategy(IDemuxStrategy&&)                 = default;
    IDemuxStrategy& operator=(IDemuxStrategy&&)      = default;

    /// Open the input stream, populating fmt_ctx.
    virtual util::ErrorEnum OpenInput(AVFormatContext*& fmt_ctx, const std::string& filename) = 0;

    /// Whether this source supports seek-based looping (VoD files only).
    virtual bool SupportsRepeat() const = 0;

    /// Whether this source is a live stream.
    virtual bool IsLive() const = 0;

    /// Whether output packets need BSF conversion (mp4toannexb).
    virtual bool NeedsBsf(VideoCodecType codec) const = 0;

    /// Whether to apply local-file FPS throttling in Demux().
    virtual bool NeedsFpsControl() const = 0;

    /// Whether to dynamically update FPS from PTS deltas.
    virtual bool ShouldUpdateFpsFromPts() const = 0;
};

}  // namespace cosmo::media
