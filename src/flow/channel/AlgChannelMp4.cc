// AlgChannelMp4 — pre-record buffer management and MP4 recording orchestration.

#include "flow/channel/AlgChannelMp4.h"

#include "flow/channel/AlgMp4Record.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

namespace cosmo {
AlgChannelMp4::AlgChannelMp4(const std::string& channel) : async_queue_("RedordQueue", 300) {
    if (channel.empty()) {
        channel_id_ = " ";
    } else {
        channel_id_ = channel;
    }
    video_encode_type_ = media::VideoCodecType::kH264;
    video_frames_.reserve(500);  // Reserve capacity for up to 500 frames to avoid reallocation on erase.
    record_tasks_.reserve(100);

    async_queue_.SetProcessor([this](VideoPacketPtr&& data) { HandleFrame(std::move(data)); });
}

AlgChannelMp4::~AlgChannelMp4() {
    record_tasks_.clear();
    video_frames_.clear();
    LOG_INFO("[MP4 CHANNEL] {} Mp4 Record Stop. ", channel_id_);
}

void AlgChannelMp4::HandleFrame(VideoPacketPtr frame) {
    if (!frame) {
        return;
    }

    if (-1 == frame->GetSequence())  // Stream playback ended; stop all tasks.
    {
        std::unique_lock<std::mutex> lock(mtx_frame_que_);
        record_tasks_.clear();
        video_frames_.clear();
        LOG_INFO("[MP4 CHANNEL] {} End Stop All Task.", channel_id_);
        return;
    }

    if (frame->GetType() != video_encode_type_) {
        LOG_INFO("[MP4 CHANNEL] {} Stream EncodeType change {} from {}.", channel_id_, frame->GetType(),
                 video_encode_type_);
        video_encode_type_ = frame->GetType();
    }

    bool b_reset = false;
    if (last_stream_index_ != frame->stream_idx) {
        // Stream restarted (mp4 loop playback increments stream_idx);
        // clear all recording tasks and pre-record buffer.
        // Old tasks hold the previous streamIndex and cannot record new stream frames.
        LOG_INFO("[MP4 CHANNEL] {} Stream Change: {} -> {}. Clear {} tasks & {} frames.", channel_id_,
                 last_stream_index_, frame->stream_idx, record_tasks_.size(), video_frames_.size());
        last_stream_index_ = frame->stream_idx;
        std::unique_lock<std::mutex> lock(mtx_frame_que_);
        record_tasks_.clear();
        video_frames_.clear();
        b_reset = true;
    } else {
        CheckParam();
        RecodeFrame(frame);
    }

    if ((last_index_ + 1 != frame->GetSequence()) && (false == b_reset)) {
        // Frame discontinuity detected.
        TaskFrameHandleDiscardFrame(frame);
        return;
    }

    TaskFrameHandleNormal(frame);
}

bool AlgChannelMp4::TaskFrame(VideoPacketPtr frame) {
    return async_queue_.Insert(frame);
}
bool AlgChannelMp4::RecordMp4(RecordParam& record_param) {
    LOG_INFO("{}", "BEBUG_ALARM");
    std::unique_lock<std::mutex> lock(mtx_frame_que_);
    auto task_start_frame_seq  = GetStartFrameIndex(record_param.streamIndex, record_param.frameTimestamp);
    record_param.startFrameSeq = task_start_frame_seq.first;
    record_param.startframeTimestamp = task_start_frame_seq.second;
    auto task = std::make_shared<AlgMp4Record>(video_encode_type_, record_param, fps_, width_, height_);
    record_tasks_.push_back(task);
    if (record_tasks_.size() > 5) {
        LOG_INFO(
            "[MP4 CHANNEL] {} Now Have {} Record Tasks. Video Gop:{} ms, Have {}=={} Frames, Insert {} "
            "Frames, Delete {} Frames.",
            channel_id_, record_tasks_.size(), last_gop_time_, video_frames_.size(),
            (push_frame_count_ - pop_frame_count_), push_frame_count_, pop_frame_count_);
    }
    return true;
}

std::pair<int64_t, int64_t> AlgChannelMp4::GetStartFrameIndex(int64_t& stream_index,
                                                              int64_t& frame_timestamp) {
    int gop_count                 = 0;
    int64_t start_frame_index     = -1;
    int64_t start_frame_timestamp = -1;
    int64_t duration =
        static_cast<int64_t>(record_pre_duration_) * 1000;  // Pre-record duration in ms; accounts for H.265
                                                            // processing delay.
    for (auto& element : video_frames_) {
        if (element->stream_idx != stream_index) {
            continue;
        }
        if (FrameIsIFrame(element)) {
            if (element->GetTimestamp() + duration > frame_timestamp) {
                if (0 == gop_count)  // No enough time found; take this GOP.
                {
                    start_frame_index     = element->GetSequence();
                    start_frame_timestamp = element->GetTimestamp();
                }

                break;
            }
            start_frame_index     = element->GetSequence();
            start_frame_timestamp = element->GetTimestamp();
            gop_count++;  // Keep this GOP.
        }
    }
    return std::make_pair(start_frame_index, start_frame_timestamp);
}

#define VALUE_SET_IN_RANGE(target, value, min, max)                                                          \
    if ((value >= min) && (value <= max))                                                                    \
        target = value;

void AlgChannelMp4::SetFps(float fps) {
    VALUE_SET_IN_RANGE(fps_, fps, 0.1f, 120.0f);
}

void AlgChannelMp4::SetSize(int width, int height) {
    VALUE_SET_IN_RANGE(width_, width, 1, media::kVideoMaxWidth);
    VALUE_SET_IN_RANGE(height_, height, 1, media::kVideoMaxHeight);
}

void AlgChannelMp4::SetChannel(const std::string& channel) {
    channel_id_ = channel;
    LOG_INFO("[MP4 CHANNEL] {} Mp4 Record Set Channel. ", channel);
}

bool AlgChannelMp4::FrameIsIFrame(VideoPacketPtr& frame) {
    // Get I-frame status from the data source; can switch to parsing frame data if inaccurate.
    return frame->IsIFrame();
}

bool AlgChannelMp4::TaskFrameHandleDiscardFrame(VideoPacketPtr& frame) {
    if (!FrameIsIFrame(frame)) {
        if (!is_wait_i_frame_) {
            LOG_INFO("[MP4 CHANNEL] {} Stream Have Discard Frame Wait For I Frame. Last:{} Now:{}",
                     channel_id_, last_index_, frame->GetSequence());
            is_wait_i_frame_ = true;
        }
        return false;
    }
    is_wait_i_frame_ = false;
    LOG_INFO("[MP4 CHANNEL] {} Stream Have Discard Frame Got I Frame FrameIndex:{}.", channel_id_,
             frame->GetSequence());

    std::unique_lock<std::mutex> lock(mtx_frame_que_);
    last_index_     = frame->GetSequence();
    last_timestamp_ = frame->GetTimestamp();
    push_frame_count_ += 1;
    video_frames_.push_back(frame);

    last_gop_time_          = frame->GetTimestamp() - last_i_frame_timestamp_;
    last_i_frame_timestamp_ = frame->GetTimestamp();  // Record last I-frame timestamp for auto-aging.

    TaskRemoveOld();

    return true;
}

bool AlgChannelMp4::TaskFrameHandleNormal(VideoPacketPtr& frame) {
    std::unique_lock<std::mutex> lock(mtx_frame_que_);
    last_index_     = frame->GetSequence();
    last_timestamp_ = frame->GetTimestamp();
    push_frame_count_ += 1;
    video_frames_.push_back(frame);

    if (FrameIsIFrame(frame)) {
        TaskRemoveOld();
        last_gop_time_          = frame->GetTimestamp() - last_i_frame_timestamp_;
        last_i_frame_timestamp_ = frame->GetTimestamp();  // Record last I-frame timestamp for auto-aging.
    }
    return true;
}

void AlgChannelMp4::TaskFrameRemoveOld(int64_t& timeStamp) {
    size_t need_pop_frame  = 0;
    size_t last_gop_frames = 0;
    size_t i_frame_index   = 0;
    int64_t duration       = static_cast<int64_t>(record_pre_duration_ + 3) *
                       1000;  // Pre-record duration + 3s buffer; accounts for H.265 processing delay.
    size_t index          = 0;
    bool b_need_pop_frame = false;

    for (auto& element : video_frames_) {
        if (FrameIsIFrame(element)) {
            i_frame_index += 1;
            // I-frame timestamp + pre-record duration > current timestamp means
            // this I-frame is too recent; keep the previous GOP.
            need_pop_frame += last_gop_frames;  // Previous GOP.

            if (element->GetTimestamp() + duration > timeStamp) {
                b_need_pop_frame = true;
                break;
            }
            need_pop_frame += 1;  // Include this frame.
            last_gop_frames = 0;

        } else {
            last_gop_frames += 1;
        }
        index++;
    }

    if (!b_need_pop_frame) {
        return;
    }
    if (need_pop_frame) {
        if (need_pop_frame > video_frames_.size()) {
            LOG_WARN("[MP4 CHANNEL] {} need_pop_frame:{} Is Lager Then FrameSize:{} ", channel_id_,
                     need_pop_frame, video_frames_.size());
            need_pop_frame = video_frames_.size();
        }
        pop_frame_count_ += static_cast<int64_t>(need_pop_frame);
        video_frames_.erase(video_frames_.begin(),
                            video_frames_.begin() + static_cast<ptrdiff_t>(need_pop_frame));

        if (need_pop_frame > video_frames_.size() / 2) {
            video_frames_.shrink_to_fit();
        }
    }
}

void AlgChannelMp4::TaskRemoveOld() {
    if (last_remove_timestamp_ != last_i_frame_timestamp_) {
        TaskFrameRemoveOld(last_i_frame_timestamp_);
        last_remove_timestamp_ = last_i_frame_timestamp_;
    }
}

void AlgChannelMp4::RecodeFrame(VideoPacketPtr& frame) {
    if (record_tasks_.empty())
        return;

    int64_t record_event_duration = static_cast<int64_t>(record_event_duration_) * 1000 +
                                    40;  // +40ms: accounts for 25fps frame timing (4960ms).

    // Create snapshot to avoid manual unlock/lock during iteration.
    std::vector<AlgMp4RecordPtr> task_snapshot;
    std::vector<VideoPacketPtr> frame_snapshot;
    {
        std::unique_lock<std::mutex> lock(mtx_frame_que_);
        task_snapshot  = record_tasks_;
        frame_snapshot = video_frames_;
    }

    for (auto& task : task_snapshot) {
        if (task->GetRecordFrames() > 0) {
            // Already recording; continue with current frame.
            if (task->GetLastIndex() + 1 == frame->GetSequence()) {
                // Frame sequence is continuous; record current frame.
                task->RecodeFrame(frame);
            } else {
                // Frame discontinuity; wait for I-frame to resume.
                if (FrameIsIFrame(frame)) {
                    task->RecodeFrame(frame);
                }
            }
        } else {
            // Not yet recording; record pre-record frames.
            int64_t task_start_frame_seq = task->GetTaskStartFrameSeq();
            for (auto& element : frame_snapshot) {
                // Skip frames before the task's pre-record start.
                if (element->GetSequence() < task_start_frame_seq) {
                    continue;
                }
                task->RecodeFrame(element);
            }
            task->RecodeFrame(frame);
        }
    }

    try {
        std::unique_lock<std::mutex> lock(mtx_frame_que_);
        record_tasks_.erase(
            std::remove_if(record_tasks_.begin(), record_tasks_.end(),
                           [this, &record_event_duration](AlgMp4RecordPtr task) {
                               if ((task->GetEventTime() != 0) &&
                                   (task->GetEventTime() + record_event_duration <= last_timestamp_)) {
                                   return true;
                               }
                               return false;
                           }),
            record_tasks_.end());
    } catch (const std::exception& e) {
        LOG_INFO("[MP4 CHANNEL] {} Erase error:{}", channel_id_, e.what());
        throw;
    }
}

void AlgChannelMp4::CheckParam() {
    auto record_info =
        service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetAlarmVideoDuration();
    if (record_pre_duration_ != static_cast<uint32_t>(record_info.preDuration)) {
        LOG_INFO("[MP4 CHANNEL] {} PreRecord Duration Change From {} To {} (Sec)", channel_id_,
                 record_pre_duration_, record_info.preDuration);
        record_pre_duration_ = static_cast<uint32_t>(record_info.preDuration);
    }

    if (record_event_duration_ != static_cast<uint32_t>(record_info.aftreDuration)) {
        LOG_INFO("[MP4 CHANNEL] {} EventRecord Duration Change From {} To {} (Sec)", channel_id_,
                 record_event_duration_, record_info.aftreDuration);
        record_event_duration_ = static_cast<uint32_t>(record_info.aftreDuration);
    }
}

}  // namespace cosmo