#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "flow/stream/RtmpCodecConfig.h"
#include "flow/stream/RtmpPacketWriter.h"
#include "media/VideoCodecType.h"
#include "media/VideoPacket.h"
#include "util/dto/OverviewTypes.h"

extern "C" {
#include "libavformat/avformat.h"
}

namespace cosmo {

/// RTMP stream pusher — Facade over RtmpCodecConfig + RtmpPacketWriter.
/// Manages the RTMP output connection lifecycle and orchestrates
/// codec configuration, NALU parsing, and packet writing.
class RtmpStreamPusher {
public:
    RtmpStreamPusher(media::VideoCodecType origin_type, const std::string& url, int width, int height,
                     float fps);
    ~RtmpStreamPusher();

    RtmpStreamPusher(const RtmpStreamPusher&)            = delete;
    RtmpStreamPusher& operator=(const RtmpStreamPusher&) = delete;

    void PushFrame(const uint8_t* data, size_t size);
    /// Raw demux packets carry immutable codec headers for their stream generation.
    /// A reconnect resets the output and waits for a new parameterized keyframe.
    void PushPacket(const media::VideoPacket& packet);
    void SetVideoAttr(int width, int height, float fps);

    /// Wait until the RTMP stream header has been pushed (first I-frame received).
    /// Returns true if ready, false on timeout.
    [[nodiscard]] bool WaitReady(std::chrono::milliseconds timeout);

    [[nodiscard]] MsgOverviewDebugInfo GetProcInfo() const {
        std::lock_guard<std::mutex> lock(output_mtx_);
        return debug_info_;
    }

    /// True only while the publisher has a live output and has successfully
    /// delivered a frame since the most recent transport failure.
    [[nodiscard]] bool IsReady() const;

    /// Close the publisher and wake readiness waiters. Idempotent and safe to
    /// call before destruction.
    void Stop();

    /// Last transport failure, suitable for diagnostics. Empty when no
    /// publisher error has occurred.
    [[nodiscard]] std::string LastError() const;

    /// Pre-populate codec config from avcC/hevcC or Annex-B extradata so HasParameters()
    /// is true immediately — no need to wait for in-band SPS/PPS.
    void SetCodecParamsFromExtradata(const std::vector<uint8_t>& extradata);

private:
    void PushFrameLocked(const uint8_t* data, size_t size);
    void SetVideoAttrLocked(int width, int height, float fps);
    int InitOutput();
    void CloseOutput();
    bool ReopenOutput();
    bool PushHeader();
    bool SetStreamReady(bool ready);
    void RecordFailure(const char* stage, int error_no);

private:
    std::string push_url_;
    media::VideoCodecType codec_type_;
    int width_{1920};
    int height_{1080};
    float fps_{25};
    MsgOverviewDebugInfo debug_info_;

    AVFormatContext* outctx_{nullptr};
    AVStream* outstream_{nullptr};
    int outindex_{0};

    std::unique_ptr<RtmpCodecConfig> codec_config_;
    std::unique_ptr<RtmpPacketWriter> packet_writer_;

    // Protects FFmpeg contexts, codec parser, packet writer and all write-side
    // lifecycle state. PushFrame can be called by raw-packet and encoder paths.
    mutable std::mutex output_mtx_;
    std::atomic<bool> stopping_{false};
    bool header_written_{false};
    std::optional<int64_t> raw_stream_index_;
    std::atomic<bool> output_failed_{false};
    int64_t skip_count_{0};
    std::atomic<bool> publisher_registered_{false};

    // Stream ready notification
    bool stream_ready_{false};
    std::string last_error_;
    mutable std::mutex ready_mtx_;
    std::condition_variable ready_cv_;
};

using RtmpStreamPusherPtr = std::shared_ptr<RtmpStreamPusher>;

}  // namespace cosmo
