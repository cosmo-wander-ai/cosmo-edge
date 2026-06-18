#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include "libavformat/avformat.h"
}

namespace cosmo {

/// Constructs and writes AVPackets to an RTMP/SRS output context.
/// Manages frame counting and timestamp rescaling.
class RtmpPacketWriter {
public:
    explicit RtmpPacketWriter(float fps);

    /// Write a single frame to the output context.
    /// @param ctx          output format context
    /// @param stream_index target stream index
    /// @param data         frame data
    /// @param size         frame data size
    /// @param is_key_frame true if this is an I-frame
    /// @return 0 on success, negative AVERROR on failure.
    [[nodiscard]] int WriteFrame(AVFormatContext* ctx, int stream_index, const uint8_t* data, size_t size,
                                 bool is_key_frame);

    void ResetCounter() {
        count_ = 0;
    }

private:
    float fps_;
    int64_t count_{0};
};

}  // namespace cosmo
