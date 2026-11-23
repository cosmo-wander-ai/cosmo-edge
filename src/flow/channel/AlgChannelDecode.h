#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
#include "media/NativePresentationOrder.h"
#endif
// AlgChannelDecode — video packet decoding, color conversion and frame distribution.

#pragma once

#include <atomic>
#include <functional>
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
#include "flow/channel/FrameInfoIndex.h"
#include "media/VideoDecoder.h"
#include "util/AsyncQueue.h"
#include "util/DurationStat.h"
#include "util/Thread.h"
#include "util/dto/AlgDataQueueTypes.h"

namespace cosmo {
class AlgChannel;
struct ChannelTaskViewerQueue {
    std::string alg_id;
    AsyncQueue<VideoFramePtr>* async_frame_queue{nullptr};
    std::function<bool()> prepare_frame;
};
using ViewerDistributionPlan = std::vector<std::string>;
class AlgChannelDecode : public AlgDataQueueDistributor, public util::Thread {
public:
    AlgChannelDecode(AlgChannel& channelInst, const std::string& channelId);
    ~AlgChannelDecode();

    bool Start();
    bool Stop();

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    // Only change before starting the decode thread or after it has joined.
    bool SetNativeFaceRequiredBeforeStart(bool required);
    bool NativeSourceIs1080p() const {
        return native_source_dimensions_.load(std::memory_order_acquire) ==
               ((std::uint64_t{1920} << 32) | 1080);
    }
    bool NativeFaceStoppedCleanly() const {
        return native_face_stopped_cleanly_.load();
    }
    bool HasNativeIncompatibleConsumers() const;
#endif

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec = 30);
    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo);

    [[nodiscard]] std::string GetName() const {
        return decoder_name_;
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

    void AddViewerFrameQueue(const std::string& algId, AsyncQueue<VideoFramePtr>& asyncFrameQueue,
                             std::function<bool()> prepareFrame = {});
    void RemoveViewerFrameQueue(const std::string& algId);

protected:
    // Thread entry point override.
    void run() override;

private:
    void HandFrame(AlgDataPtr demuxData);
    bool ValidateFrame(VideoPacketPtr& videoFrame, bool justNeedIFrame);
    void PrepareDecoder(VideoPacketPtr& videoFrame);
    bool NeedsResize(VideoPacketPtr& videoFrame);
    AlgDataPtr ColorConvert(AlgDataPtr demuxData, VideoFramePtr inData,
                            media::NativeVideoBufferPtr nativeBuffer = nullptr);

    void FrameInfoSave(const AlgDataPtr& data);
    AlgFrameInfo FrameInfoGet(int64_t index);

    // Image capture helpers.
    void DoCaptureImage(VideoFramePtr inData);
    void CaptureJpeg(VideoFramePtr picture);

    // Distribute frames to display viewers.
    void DistributeViewer(VideoFramePtr inData);
    ViewerDistributionPlan PrepareViewerDistribution();
    void DistributePreparedViewer(const ViewerDistributionPlan& plan, VideoFramePtr inData);
    bool NeedsHostFrame(int64_t streamIndex);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    bool HasPendingCapture();
#endif

private:
    mutable std::mutex mtx_;
    AlgChannel& channel_inst_;
    std::string channel_id_;
    std::string decoder_name_;
    std::string uuid_;
    // Image capture state.
    std::condition_variable cap_cond_;
    bool is_capturing_{false};
    VideoFramePtr cap_data_;

    bool codec_reset_sign_{false};  // Decoder threw exception; needs rebuild.
    int64_t frame_index_{-1};
    int64_t stream_index_{-1};
    int64_t decode_count_{0};
    uint64_t decoder_stream_reuse_count_{0};
    int64_t cap_image_stream_index_{-1};
    std::atomic<bool> is_running_{false};
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    std::atomic<std::uint64_t> native_source_dimensions_{0};
    std::atomic<bool> native_face_required_{false};
    std::atomic<bool> native_face_stopped_cleanly_{true};
#endif
    int64_t consecutive_decode_failures_{0};  // Consecutive decode failure count
    static constexpr int64_t kMaxConsecutiveDecodeFailures =
        10;  // Threshold to trigger automatic decoder rebuild.

    std::unique_ptr<media::VideoDecoder> decoder_;  // Deferred creation.
    int device_id_{0};                              // Device ID for deferred decoder construction.

    util::ErrorEnum action_status_{util::ErrorEnum::Success};
    util::DurationStat duration_stat_;
    std::vector<ChannelTaskViewerQueue> viewer_queue_;
    std::deque<AlgFrameInfo> frame_info_;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    media::NativePresentationOrder native_presentation_order_;
#endif
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> dec_queue_;
};
}  // namespace cosmo
