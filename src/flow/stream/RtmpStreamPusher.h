#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "flow/stream/RtmpCodecConfig.h"
#include "flow/stream/RtmpPacketWriter.h"
#include "flow/stream/VideoStreamPush.h"
#include "media/VideoCodecType.h"

extern "C" {
#include "libavformat/avformat.h"
}

namespace cosmo {

/// RTMP stream pusher — Facade over RtmpCodecConfig + RtmpPacketWriter.
/// Manages the RTMP output connection lifecycle and orchestrates
/// codec configuration, NALU parsing, and packet writing.
class RtmpStreamPusher : public VideoStreamPush {
public:
    RtmpStreamPusher(media::VideoCodecType origin_type, const std::string& url, int width, int height,
                     float fps);
    ~RtmpStreamPusher() override;

    RtmpStreamPusher(const RtmpStreamPusher&)            = delete;
    RtmpStreamPusher& operator=(const RtmpStreamPusher&) = delete;

    /// Wait until the RTMP stream header has been pushed (first I-frame received).
    /// Returns true if ready, false on timeout.
    [[nodiscard]] bool WaitReady(std::chrono::milliseconds timeout);

private:
    void DoPushFrame(const uint8_t* data, size_t size) override;

    int InitOutput();
    void CloseOutput();
    bool ReopenOutput();
    void PushHeader();

private:
    AVFormatContext* outctx_{nullptr};
    AVStream* outstream_{nullptr};
    int outindex_{0};

    std::unique_ptr<RtmpCodecConfig> codec_config_;
    std::unique_ptr<RtmpPacketWriter> packet_writer_;

    std::atomic_flag first_frame_flag_;
    bool output_failed_{false};
    int64_t skip_count_{0};

    // Stream ready notification
    bool stream_ready_{false};
    std::mutex ready_mtx_;
    std::condition_variable ready_cv_;
};

using RtmpStreamPusherPtr = std::shared_ptr<RtmpStreamPusher>;

}  // namespace cosmo
