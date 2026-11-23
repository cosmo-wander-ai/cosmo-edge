#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include "libavformat/avformat.h"
}

namespace cosmo {

namespace media {
    struct VideoPacket;
}

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
                                 bool is_key_frame, const media::VideoPacket* source = nullptr);

    void ResetCounter();

private:
    float fps_;
    int64_t count_{0};
    bool timing_mode_set_{false};
    bool source_timing_{false};
    int64_t origin_dts_{0};
    int source_time_base_num_{0};
    int source_time_base_den_{0};
    int64_t last_output_dts_{AV_NOPTS_VALUE};
};

}  // namespace cosmo
