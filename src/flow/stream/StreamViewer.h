#pragma once

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <shared_mutex>
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
    StreamViewer(AlgChannelPtr channelInst, const std::string& channelId, const std::string& algId);
    ~StreamViewer();

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
        if (video_pusher_) {
            return video_pusher_->WaitReady(timeout);
        }
        return false;
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

    void UpdateCtrlFps();
    bool EncoderReady() const;

    std::shared_mutex mtx_;
    std::string channel_id_;
    std::string alg_id_;
    AlgChannelPtr channel_inst_;
    bool data_packet_{false};    // Whether to use async_packet_queue_ queue
    bool data_overview_{false};  // Whether overlay is needed

    int64_t heartbeat_timestamp_{0};     // Heartbeat timestamp
    int heartbeat_failed_count_{0};      // Continuous heartbeat failure count
    int64_t heartbeat_duration_{10000};  // Heartbeat interval
    int heartbeat_failed_interval_{6};   // Disconnect on continuous heartbeat failures

    std::atomic<int> viewer_num_{1};  // Number of viewers for the same camera

    // Frame rate control
    float ctrl_fps_{13.0};
    float in_fps_{0.0};
    util::FpsCtrl out_fps_ctl_;
    util::FpsCalc input_fps_calc_;  // Input FPS calculator
    size_t data_index_{0};

    RtmpStreamPusherPtr video_pusher_{nullptr};
    StreamViewerOverviewPtr overviewer_{nullptr};
    StreamViewerEncoderPtr encoder_{nullptr};

    // H264 data
    std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue_;
    // YUV data
    AsyncQueue<VideoFramePtr> async_frame_queue_;
};
using StreamViewerPtr = std::shared_ptr<StreamViewer>;
}  // namespace cosmo
