// Channel demuxer — reads video stream via FFmpeg and distributes
// packets to downstream decode/record pipelines.

#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/action/AlgActionBase.h"
#include "flow/channel/AlgChannelMp4.h"
#include "flow/common/AlgDataQueueDistributor.h"
#include "media/VideoDemuxer.h"
#include "media/VideoPacket.h"
#include "util/DurationStat.h"
#include "util/Thread.h"
#include "util/dto/ChannelStatusDto.h"
#include "util/dto/CosmoFwd.h"

namespace cosmo {

class AlgChannelDemux : public AlgDataQueueDistributor, public util::Thread {
public:
    explicit AlgChannelDemux(const std::string& channelId, const std::string& url = "");
    ~AlgChannelDemux();

    void ActionInfo(std::vector<ActionRuntimeInfo>& actionInfo);

    bool SetUrl(const std::string& url);
    bool SetVideoRepeatCount(int repeatCount);
    bool SetVideoFps(float fps);
    bool SetPollChannel(const std::string& channelId);
    [[nodiscard]] std::string GetUrl() const {
        return url_;
    };

    [[nodiscard]] bool IsLiveStream() const;

    // frameSeq: frame sequence (input)
    // frameTimestamp: timestamp (input)
    // startFrameSeq: start frame sequence (output)
    bool RecordMp4(RecordParam& recordParam);

    void Start();
    void Stop();

    void QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec);

    [[nodiscard]] VideoPacketPtr GetLastFrame() const;
    void ClearLastFrame();

    [[nodiscard]] bool IsDataActive() const;

    [[nodiscard]] size_t TotalReadFrame() const {
        return read_frames_;
    };

    [[nodiscard]] util::DurationStatInfo GetDurationInfo(int durationMs = 5000) {
        return duration_stat_.ComputeStats(durationMs);
    }

    [[nodiscard]] util::ErrorEnum GetStatus() const {
        return action_status_;
    }

    [[nodiscard]] const std::vector<uint8_t>& GetCodecExtradata() const {
        return demuxer_.GetCodecExtradata();
    }

    [[nodiscard]] media::VideoCodecType GetVideoCodecType() const {
        return demuxer_.GetEncodeType();
    }

    bool GetAttr(MsgCameraAttr& attr);

    bool SetForStartTask();

    void AddViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> asyncPacketQueue);
    void RemoveViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> asyncPacketQueue);

protected:
    // Thread entry point override.
    void run() override;

private:
    bool OpenStream();
    void CloseStream();
    void HandleStream();
    std::string StatusString(service::camera::AlgDemuxStatus status);
    void SetStatusInfo(service::camera::AlgDemuxStatus status);
    void RequestUrl();
    void NotifyOnComplete();
    void NotifyOnInfo();

private:
    mutable std::mutex lifecycle_mtx_;
    mutable std::shared_mutex demux_mtx_;
    std::string channel_id_;
    std::string poll_channel_id_;  // Polling channel ID
    std::string url_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_url_changed_{false};
    std::atomic<bool> is_need_repeat_{false};
    bool is_have_report_{false};
    std::atomic<bool> is_opened_{false};
    service::camera::AlgDemuxStatusInfo status_{};
    int video_repeat_count_{1};  // Maximum video repeat count
    int video_read_count_{0};    // Current repeat iteration
    int no_data_count_{0};
    int open_failed_count_{0};  // Consecutive open-failure count
    size_t read_frames_{0};
    mutable size_t check_active_frame_{0};
    mutable int64_t check_active_ts_{0};
    VideoPacketPtr last_frame_;
    mutable util::ErrorEnum action_status_{util::ErrorEnum::Success};
    std::vector<std::shared_ptr<AsyncQueue<VideoPacketPtr>>> async_packet_queues_;
    AlgChannelMp4Ptr recorder_;
    media::VideoDemuxer demuxer_;
    util::DurationStat duration_stat_;
};
}  // namespace cosmo
