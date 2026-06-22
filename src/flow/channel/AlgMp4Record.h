// AlgMp4Record — single MP4 recording task with NALU parsing and file upload.

#pragma once
#include <memory>
#include <string>
#include <vector>

#include "flow/channel/BufferPool.h"
#include "flow/common/AlgDataUnit.h"

typedef void* MP4FileHandle;
typedef uint32_t MP4TrackId;

namespace cosmo {

class AlgMp4Record {
public:
    AlgMp4Record(media::VideoCodecType streamType, const RecordParam& recordParam, float fps, int width,
                 int height);
    ~AlgMp4Record();

    bool RecodeFrame(VideoPacketPtr frame);

    int64_t GetRecordFrames();
    int64_t GetLastIndex();
    int64_t GetEventTime();
    int64_t GetTaskStartFrameSeq();
    std::string GetEventName();

private:
    // NALU-type handlers extracted from RecodeFrame (DEBT-011)
    bool HandleVps(const uint8_t* frame_data, size_t frame_size, int64_t seq, bool is_iframe);
    bool HandleSps(const uint8_t* frame_data, size_t frame_size, int64_t seq, bool is_iframe);
    bool HandlePps(const uint8_t* frame_data, size_t frame_size, int64_t seq, bool is_iframe);
    bool WriteVideoSample(const uint8_t* data, size_t size, size_t offset, size_t sz, size_t moved_size,
                          int64_t seq, bool is_iframe);

    std::vector<uint8_t> TransformFrame(const uint8_t* data, size_t size);
    void SetTrack(const uint8_t* sps, size_t size);
    std::string GetPath();
    void UploadJsonFile();
    void UploadOverviewFile();

private:
    MP4FileHandle mp4_handle_;
    MP4TrackId track_id_;
    bool is_vps_ready_{false};
    bool is_sps_ready_{false};
    bool is_pps_ready_{false};

    media::VideoCodecType source_type_;
    std::string channel_id_;
    std::string task_id_;
    int target_id_{-1};
    float fps_{25.0};
    int width_{1920};
    int height_{1080};
    size_t max_frame_size_{0};

    int64_t task_start_frame_seq_{0};   // First frame sequence of the task
    int64_t task_start_frame_time_{0};  // Timestamp of the task's first frame
    int64_t start_time_{0};             // Timestamp when the alarm was raised
    int64_t start_index_{0};            // Frame sequence when the alarm was raised
    int64_t last_time_{0};
    int64_t last_index_{-1};  // Start sequence may be 0
    int64_t event_time_{0};
    int64_t alarm_timestamp_{0};
    int64_t event_index_{0};
    int64_t stream_index_{-1};
    int64_t record_frames_{0};
    int64_t unknown_frame_type_count_{0};
    int64_t stream_mismatch_count_{0};  // Throttled mismatch counter for logging
    int64_t total_size_{0};             // Total bytes written to MP4
    int64_t data_size_{0};              // Total bytes received as input
    std::string event_name_;
    std::string file_name_;
    std::string mp4_name_;
    std::string upload_url_;
    std::string upload_json_url_;
    std::string json_path_;
    std::string overview_json_url_;
    MsgAlarmVideoOverviewInfo overview_info_;                 // Region data for UploadOverviewFile
    RetroDirect retro_direct_{RetroDirect::RetroDirectNone};  // No direction
    flow::BufferPool buffer_pool_;                            // Per-instance buffer pool for TransformFrame
};

}  // namespace cosmo
