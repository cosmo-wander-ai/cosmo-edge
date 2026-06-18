// AlgChannelDecode — video packet decoding, color conversion and frame distribution.

#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#ifdef __cplusplus
}
#endif

#include "flow/action/AlgActionBase.h"
#include "media/VideoDecoder.h"
#include "util/AsyncQueue.h"
#include "util/DurationStat.h"
#include "util/Thread.h"
#include "util/dto/AlgDataQueueTypes.h"

namespace cosmo {
class AlgChannel;
struct AlgFrameInfo {
    int64_t index{-1};
    int64_t streamIndex{-1};
    int64_t timestamp{-1};
};

struct ChannelTaskViewerQueue {
    std::string alg_id;
    AsyncQueue<VideoFramePtr>* async_frame_queue{nullptr};
};
class AlgChannelDecode : public AlgDataQueueDistributor, public util::Thread {
public:
    AlgChannelDecode(AlgChannel& channelInst, const std::string& channelId);
    ~AlgChannelDecode();

    void Start();
    void Stop();

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec = 30);
    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo);

    [[nodiscard]] std::string GetName() const {
        return name_;
    }
    [[nodiscard]] std::string GetUuid() const {
        return uuid_;
    }
    [[nodiscard]] float GetRequiredFps() const {
        return -1.0f;
    }  // Decode at full frame rate.

    [[nodiscard]] util::DurationStatInfo GetDurationInfo(int durationMs = 5000) {
        return duration_stat_.ComputeStats(durationMs);
    }
    [[nodiscard]] std::shared_ptr<AlgDataQueue<AlgDataPtr>> GetQueue() const {
        return dec_queue_;
    }

    [[nodiscard]] util::ErrorEnum GetStatus() const {
        return action_status_;
    }

    // Public interface for image capture.
    VideoFramePtr CaptureImage(int timeoutMs = 3000);

    void AddViewerFrameQueue(const std::string& algId, AsyncQueue<VideoFramePtr>& asyncFrameQueue);
    void RemoveViewerFrameQueue(const std::string& algId);

protected:
    // Thread entry point override.
    void run() override;

private:
    void HandFrame(AlgDataPtr demuxData);
    bool ValidateFrame(VideoPacketPtr& videoFrame, bool justNeedIFrame);
    void PrepareDecoder(VideoPacketPtr& videoFrame);
    bool NeedsResize(VideoPacketPtr& videoFrame);
    AlgDataPtr ColorConvert(AlgDataPtr demuxData, VideoFramePtr inData);

    void FrameInfoSave(VideoPacketPtr packet);
    AlgFrameInfo FrameInfoGet(int64_t index);

    // Image capture helpers.
    void DoCaptureImage(VideoFramePtr inData);
    void CaptureJpeg(VideoFramePtr picture);

    // Distribute frames to display viewers.
    void DistributeViewer(VideoFramePtr inData);

private:
    mutable std::mutex mtx_;
    AlgChannel& channel_inst_;
    std::string channel_id_;
    std::string name_;
    std::string uuid_;
    // Image capture state.
    std::condition_variable cap_cond_;
    bool is_capturing_{false};
    VideoFramePtr cap_data_;

    bool codec_reset_sign_{false};  // Decoder threw exception; needs rebuild.
    int64_t frame_index_{-1};
    int64_t stream_index_{-1};
    int64_t decode_count_{0};
    int64_t cap_image_stream_index_{-1};
    float fps_{0.0};
    std::atomic<bool> is_running_{false};
    int64_t consecutive_decode_failures_{0};  // Consecutive decode failure count
    static constexpr int64_t kMaxConsecutiveDecodeFailures =
        10;  // Threshold to trigger automatic decoder rebuild.

    std::unique_ptr<media::VideoDecoder> decoder_;  // Deferred creation.
    int device_id_{0};                              // Device ID for deferred decoder construction.

    util::ErrorEnum action_status_{util::ErrorEnum::Success};
    util::DurationStat duration_stat_;
    std::vector<ChannelTaskViewerQueue> viewer_queue_;
    std::deque<AlgFrameInfo> frame_info_;
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> dec_queue_;

    int width_{0};
    int height_{0};
};
}  // namespace cosmo
