#pragma once

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flow/channel/AlgChannel.h"
#include "flow/stream/RtmpStreamPusher.h"
#include "flow/stream/StreamViewerEncoder.h"
#include "flow/stream/StreamViewerOverview.h"
#include "media/VideoPacket.h"
#include "util/AsyncQueue.h"

namespace cosmo {

class StreamViewer {
public:
    /// force_encode_raw selects current decoded frames for a raw preview whose
    /// passthrough publisher could not receive a fresh keyframe in time.
    StreamViewer(AlgChannelPtr channelInst, const std::string& channelId, const std::string& algId,
                 bool force_encode_raw = false);
    ~StreamViewer();

    StreamViewer(const StreamViewer&)            = delete;
    StreamViewer& operator=(const StreamViewer&) = delete;

    /// Detach from the channel, drain worker callbacks and close the RTMP
    /// publisher. Safe to call repeatedly from timeout/error/stop paths.
    void Stop();

    /// Mark the stream as externally ready after its first frame reaches SRS.
    /// The matching lifecycle metric is released exactly once by Stop().
    void MarkReady(std::chrono::nanoseconds first_frame_latency);

    std::string GetChannelId() const {
        return channel_id_;
    }
    std::string GetAlgId() const {
        return alg_id_;
    }
    void HeartBeat();

    bool HeartBeatCheck();

    bool HaveEncoder();

    /// Wait until the underlying RTMP stream is ready.
    bool WaitReady(std::chrono::milliseconds timeout) {
        if (!stopped_.load() && video_pusher_) {
            return video_pusher_->WaitReady(timeout);
        }
        return false;
    }

    [[nodiscard]] std::string LastPublishError() const {
        return video_pusher_ ? video_pusher_->LastError() : std::string{};
    }

    [[nodiscard]] bool IsPublishReady() const {
        return !stopped_.load() && video_pusher_ && video_pusher_->IsReady();
    }

    [[nodiscard]] bool IsStopped() const {
        return stopped_.load();
    }

    int GetViewerNum() const {
        return viewer_num_.load();
    }

    void UpViewerNum() {
        viewer_num_.fetch_add(1);
    }

    void DelViewerNum() {
        int old = viewer_num_.load();
        while (old > 0 && !viewer_num_.compare_exchange_weak(old, old - 1)) {
        }
    }

private:
    void HandlePacket(VideoPacketPtr Frame);
    void HandleFrame(VideoFramePtr Frame);
    bool PrepareFrameForDecode();

    void UpdateCtrlFps();
    bool EncoderReady() const;

    std::string channel_id_;
    std::string alg_id_;
    AlgChannelPtr channel_inst_;
    bool data_packet_{false};    // Whether to use async_packet_queue_ queue
    bool data_overview_{false};  // Whether overlay is needed

    int64_t heartbeat_timestamp_{0};     // Heartbeat timestamp
    int heartbeat_failed_count_{0};      // Continuous heartbeat failure count
    int64_t heartbeat_duration_{10000};  // Heartbeat interval
    int heartbeat_failed_interval_{6};   // Disconnect on continuous heartbeat failures
    std::mutex heartbeat_mtx_;

    std::atomic<int> viewer_num_{1};  // Number of viewers for the same camera

    // Frame rate control
    float ctrl_fps_{13.0};
    float in_fps_{0.0};
    util::FpsCtrl out_fps_ctl_;
    util::FpsCalc input_fps_calc_;  // Input FPS calculator
    size_t data_index_{0};
    std::mutex frame_filter_mtx_;

    RtmpStreamPusherPtr video_pusher_{nullptr};
    StreamViewerOverviewPtr overviewer_{nullptr};
    StreamViewerEncoderPtr encoder_{nullptr};

    // H264 data
    std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue_;
    // YUV data
    AsyncQueue<VideoFramePtr> async_frame_queue_;

    std::mutex stop_mtx_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> preview_ready_{false};
    bool packet_queue_attached_{false};
    bool frame_queue_attached_{false};
};
using StreamViewerPtr = std::shared_ptr<StreamViewer>;
}  // namespace cosmo
