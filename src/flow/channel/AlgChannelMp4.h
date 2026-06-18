// AlgChannelMp4 — pre-record buffer and MP4 recording task manager.

#pragma once
#include <list>
#include <mutex>
#include <queue>
#include <thread>

#include "flow/common/AlgDataUnit.h"
#include "media/VideoPacket.h"
#include "util/AsyncQueue.h"

namespace cosmo {
class AlgMp4Record;

using AlgMp4RecordPtr = std::shared_ptr<AlgMp4Record>;

class AlgChannelMp4 {
public:
    explicit AlgChannelMp4(const std::string& channel);
    ~AlgChannelMp4();

    // Insert a new frame into the pre-record buffer.
    bool TaskFrame(VideoPacketPtr frame);

    // frameSeq: frame sequence (input)
    // frameTimestamp: timestamp (input)
    // startFrameSeq: start frame sequence (output)
    bool RecordMp4(RecordParam& record_param);

    void SetFps(float fps);
    void SetSize(int width, int height);

    void SetChannel(const std::string& channel);  // Set channel after delayed load
                                                  // (CameraEntityManager::SetCameraId).

private:
    bool FrameIsIFrame(VideoPacketPtr& frame);

    void HandleFrame(VideoPacketPtr frame);

    // Frame insertion handlers.
    bool TaskFrameHandleNewStream(VideoPacketPtr& frame);     // New stream detected during frame insert.
    bool TaskFrameHandleDiscardFrame(VideoPacketPtr& frame);  // Frame discontinuity detected.
    bool TaskFrameHandleNormal(VideoPacketPtr& frame);        // Normal frame insertion.
    void TaskFrameRemoveOld(int64_t& timeStamp);  // Remove expired frames based on pre-record duration.
    void TaskFrameRemoveForNewStream();           // Clear all frames on stream switch or replay.
    void TaskRemoveOld();                         // Auto-age frames.

    // Get the start frame index for a recording task at alarm time.
    [[nodiscard]] std::pair<int64_t, int64_t> GetStartFrameIndex(int64_t& stream_index,
                                                                 int64_t& frame_timestamp);

    // Recording task management.
    void Record();

    void RecodeFrame(VideoPacketPtr& frame);
    // Validate recording parameters.
    void CheckParam();

private:
    std::string channel_id_;
    std::mutex mtx_frame_que_;
    std::thread thr_;
    bool is_wait_i_frame_{false};  // Log throttle flag.

    bool is_need_delete_frame_{false};   // Pending frame deletion.
    int64_t delete_to_index_{0};         // Delete up to this index.
    int64_t delete_to_timestamp_{0};     // Delete up to this timestamp.
    int64_t last_i_frame_timestamp_{0};  // Last I-frame timestamp for auto-aging.
    int64_t last_remove_timestamp_{0};   // Timestamp of last aging removal.
    int64_t last_index_{0};
    int64_t last_stream_index_{0};
    int64_t last_timestamp_{0};
    uint32_t record_pre_duration_{5};
    uint32_t record_event_duration_{5};

    int64_t last_gop_time_{0};
    float fps_{25};
    int width_{1920};
    int height_{1080};

    media::VideoCodecType video_encode_type_;
    std::vector<VideoPacketPtr> video_frames_;
    std::vector<AlgMp4RecordPtr> record_tasks_;

    int64_t push_frame_count_{0};
    int64_t pop_frame_count_{0};
    AsyncQueue<VideoPacketPtr> async_queue_;
};

using AlgChannelMp4Ptr = std::shared_ptr<AlgChannelMp4>;
}  // namespace cosmo
