// AlgMp4RecordWriter.cc — Recording and writing for AlgMp4Record.
// Split from AlgMp4Record.cc to reduce file size (DEBT-007).

#include <filesystem>

#include "flow/channel/AlgMp4Record.h"
#include "media/VideoUtil.h"
#include "mp4v2/mp4v2.h"
#include "mp4v2/track.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IFileService.h"
#include "service/task/ITaskQuery.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/PathUtil.h"

#define VALUE_SET_IN_RANGE(target, value, min, max)                                                          \
    if ((value >= min) && (value <= max))                                                                    \
        target = value;

namespace cosmo {

constexpr uint32_t timeScale = 90000;

// ---------------------------------------------------------------------------
// NALU-type handlers extracted from RecodeFrame (DEBT-011)
// ---------------------------------------------------------------------------

bool AlgMp4Record::HandleVps(const uint8_t* frame_data, size_t frame_size, int64_t seq, bool is_iframe) {
    SetTrack(frame_data, frame_size);
    if (MP4_INVALID_TRACK_ID == track_id_) {
        LOG_WARN("[MP4 TASK] {} VPS {} Frame:{} bIFrame:{} Track Not Ready. ", task_id_, event_name_, seq,
                 is_iframe);
        return false;
    }
    if (is_vps_ready_) {
        return false;
    }
    total_size_ += static_cast<int64_t>(frame_size);
    MP4AddH265VideoParameterSet(mp4_handle_, track_id_, frame_data, static_cast<uint16_t>(frame_size));
    is_vps_ready_ = true;
    return false;
}

bool AlgMp4Record::HandleSps(const uint8_t* frame_data, size_t frame_size, int64_t seq, bool is_iframe) {
    SetTrack(frame_data, frame_size);
    if (MP4_INVALID_TRACK_ID == track_id_) {
        LOG_WARN("[MP4 TASK] {} SPS {} Frame:{} bIFrame:{} Track Not Ready. ", task_id_, event_name_, seq,
                 is_iframe);
        return false;
    }
    if (is_sps_ready_) {
        return false;
    }
    total_size_ += static_cast<int64_t>(frame_size);
    if (media::VideoCodecType::kH265 == source_type_) {
        MP4AddH265SequenceParameterSet(mp4_handle_, track_id_, frame_data, static_cast<uint16_t>(frame_size));
    } else {
        MP4AddH264SequenceParameterSet(mp4_handle_, track_id_, frame_data, static_cast<uint16_t>(frame_size));
    }
    is_sps_ready_ = true;
    return false;
}

bool AlgMp4Record::HandlePps(const uint8_t* frame_data, size_t frame_size, int64_t seq, bool is_iframe) {
    if (MP4_INVALID_TRACK_ID == track_id_) {
        LOG_WARN("[MP4 TASK] {} {} Frame:{} bIFrame:{} Track Not Ready. ", task_id_, event_name_, seq,
                 is_iframe);
        return false;
    }
    if (is_pps_ready_) {
        return false;
    }
    total_size_ += static_cast<int64_t>(frame_size);
    if (media::VideoCodecType::kH265 == source_type_) {
        MP4AddH265PictureParameterSet(mp4_handle_, track_id_, frame_data, static_cast<uint16_t>(frame_size));
    } else {
        MP4AddH264PictureParameterSet(mp4_handle_, track_id_, frame_data, static_cast<uint16_t>(frame_size));
    }
    is_pps_ready_ = true;
    return false;
}

bool AlgMp4Record::WriteVideoSample(const uint8_t* data, size_t size, size_t offset, size_t sz,
                                    size_t moved_size, int64_t seq, bool is_iframe) {
    if (MP4_INVALID_TRACK_ID == track_id_) {
        LOG_WARN("[MP4 TASK] {} {} Frame:{} bIFrame:{} Track Not Ready. ", task_id_, event_name_, seq,
                 is_iframe);
        return false;
    }

    std::vector<uint8_t> buffer;
    try {
        // Transform remaining data starting from current NALU offset
        buffer = TransformFrame(data + offset, size - offset);
    } catch (const std::exception& e) {
        LOG_ERRO("[MP4 TASK] {} {} Frame:{} TransformFrame error:{} offset:{} size:{}", task_id_, event_name_,
                 seq, e.what(), offset, size);
        return false;
    }

    if (buffer.empty()) {
        LOG_WARN("[MP4 TASK] {} {} Frame:{} size:{} offset:{} sz:{} < movedSize:{} TransformFrame empty",
                 task_id_, event_name_, seq, size, offset, sz, moved_size);
        return false;
    }

    try {
        total_size_ += static_cast<int64_t>(buffer.size());
        MP4WriteSample(mp4_handle_, track_id_, buffer.data(), static_cast<uint32_t>(buffer.size()));
    } catch (const std::exception& e) {
        LOG_INFO("[MP4 TASK] {} {} Frame:{} MP4WriteSample error:{}", task_id_, event_name_, seq, e.what());
        throw;
    }

    // A single frame may contain multiple I-frame NALUs; writing them as a
    // single sample produces a playable MP4, while splitting does not.
    record_frames_ += 1;
    return true;
}

// ---------------------------------------------------------------------------
// RecodeFrame — NALU iteration loop (refactored from ~210 lines to ~80)
// ---------------------------------------------------------------------------

bool AlgMp4Record::RecodeFrame(VideoPacketPtr frame) {
    if (!frame) {
        LOG_WARN("[MP4 TASK] {} {} Frame is Empty. Last Frame:{}", task_id_, event_name_, last_index_);
        return false;
    }
    // Bail out if the MP4 handle was never created
    if (!mp4_handle_) {
        LOG_WARN("[MP4 TASK] {} {} Record Frame:{} MP4 Create Fail.", task_id_, event_name_,
                 frame->GetSequence());
        return false;
    }

    if (frame->stream_idx != stream_index_) {
        // Stream index mismatch means the source has restarted (indices are
        // monotonically increasing).  This MP4 task was created for the old
        // stream and cannot record frames from the new one.
        // Note: throttle logging to once per 100 mismatched frames.
        stream_mismatch_count_++;
        if (stream_mismatch_count_ == 1 || stream_mismatch_count_ % 100 == 0) {
            LOG_WARN("[MP4 TASK] {} {} Stream Mismatch (x{}): Frame={} FrameStream={} expect={}, skip.",
                     task_id_, event_name_, stream_mismatch_count_, frame->GetSequence(), frame->stream_idx,
                     stream_index_);
        }
        return false;
    }

    if ((frame->GetSize() == 0) || (frame->GetSize() > max_frame_size_)) {
        LOG_WARN("[MP4 TASK] {} {} Record Frame:{}, dataSize:{}. MaxSize:{}", task_id_, event_name_,
                 frame->GetSequence(), frame->GetSize(), max_frame_size_);
        return false;
    }
    data_size_ += static_cast<int64_t>(frame->GetSize());

    const bool is_iframe = frame->IsIFrame();

    if ((0 == start_index_) && (0 == start_time_)) {
        start_index_ = frame->GetSequence();
        start_time_  = frame->GetTimestamp();
    }
    last_index_ = frame->GetSequence();
    last_time_  = frame->GetTimestamp();

    size_t offset = 0;  // Current byte offset into frame data
    size_t sz     = 0;  // Current NALU size

    size_t size       = frame->GetSize();
    uint8_t* data     = frame->GetData();
    const int64_t seq = frame->GetSequence();

    int nalu_count = 0;
    try {
        do {
            offset += sz;
            // Locate the next NALU boundary
            sz = media::SeparateHVideoFrame(data + offset, size - offset);
            if (sz <= 4) {
                // Guard against degenerate NALU: 00 00 00 01 00 00 00 01 42
                if (sz == 0) {
                    return false;
                }
                continue;
            }

            // Strip the 00 00 00 01 start-code prefix
            size_t moved_size = 0;
            auto frame_data   = media::RemoveHFrameSeparator(data + offset, moved_size);
            if (moved_size >= sz) {
                LOG_WARN("[MP4 TASK] {} {} Frame:{} size:{} offset:{} sz:{} < movedSize:{} ", task_id_,
                         event_name_, seq, size, offset, sz, moved_size);
                return false;
            }
            const size_t frame_size = sz - moved_size;

            // Dispatch by NALU type
            switch (media::GetFrameType(source_type_, data + offset)) {
                case media::HFrameType::VPS:
                    HandleVps(frame_data, frame_size, seq, is_iframe);
                    break;
                case media::HFrameType::SPS:
                    HandleSps(frame_data, frame_size, seq, is_iframe);
                    break;
                case media::HFrameType::PPS:
                    HandlePps(frame_data, frame_size, seq, is_iframe);
                    break;
                case media::HFrameType::SEI:
                    break;
                case media::HFrameType::P:
                case media::HFrameType::I:
                    if (WriteVideoSample(data, size, offset, sz, moved_size, seq, is_iframe)) {
                        return true;
                    }
                    return false;
                default:
                    unknown_frame_type_count_++;
                    // Unknown NALU type at the head — skip it
                    data += sz;
                    size -= sz;
                    offset = 0;
                    sz     = 0;
                    break;
            }
            nalu_count += 1;
        } while (offset + sz != size);
    } catch (const std::exception& e) {
        LOG_INFO("[MP4 TASK] {} {} Frame:{} MP4 recording error:{} offset:{} sz:{} size:{} count:{}",
                 task_id_, event_name_, seq, e.what(), offset, sz, size, nalu_count);
        throw;
    }
    record_frames_ += 1;
    return true;
}

int64_t AlgMp4Record::GetLastIndex() {
    return last_index_;
}

int64_t AlgMp4Record::GetRecordFrames() {
    return record_frames_;
}

int64_t AlgMp4Record::GetEventTime() {
    return event_time_;
}

int64_t AlgMp4Record::GetTaskStartFrameSeq() {
    return task_start_frame_seq_;
}

std::string AlgMp4Record::GetEventName() {
    return event_name_;
}

std::vector<uint8_t> AlgMp4Record::TransformFrame(const uint8_t* data, size_t size) {
    size_t movedHead = 0;
    auto dataBeg     = media::RemoveHFrameSeparator(data, movedHead);
    if (nullptr == dataBeg) {
        LOG_WARN("[MP4 TASK] {} {} FindHeard Failed, data Size:{}.", task_id_, event_name_, size);
        return {};
    }
    auto dataSizeSigned = data + size - dataBeg;
    if ((dataSizeSigned > static_cast<ptrdiff_t>(max_frame_size_)) || (dataSizeSigned < 0)) {
        LOG_WARN("[MP4 TASK] {} {} FindHeard Failed, data Size:{} dataSize:{}.", task_id_, event_name_, size,
                 dataSizeSigned);
        return {};
    }
    auto dataSize = static_cast<size_t>(dataSizeSigned);
    // Acquire a reusable buffer from the pool
    auto buffer = buffer_pool_.AcquireBuffer(dataSize + 4);
    if (!buffer) {
        LOG_WARN("[MP4 TASK] {} {} Failed to acquire buffer from pool", task_id_, event_name_);
        return std::vector<uint8_t>();
    }

    try {
        if (buffer->capacity() < dataSize + 4) {
            buffer->reserve(dataSize + 4);
        }
        buffer->resize(dataSize + 4);

        // Write MP4 NALU length prefix (big-endian 4 bytes)
        (*buffer)[0] = (dataSize >> 24) & 0xFF;
        (*buffer)[1] = (dataSize >> 16) & 0xFF;
        (*buffer)[2] = (dataSize >> 8) & 0xFF;
        (*buffer)[3] = (dataSize >> 0) & 0xFF;
        memcpy(buffer->data() + 4, dataBeg, dataSize);

        std::vector<uint8_t> result = *buffer;

        // Return buffer to pool
        buffer_pool_.ReleaseBuffer(std::move(buffer));

        return result;
    } catch (const std::exception& e) {
        // Return buffer to pool even on exception
        buffer_pool_.ReleaseBuffer(std::move(buffer));
        LOG_ERRO("[MP4 TASK] {} {} TransformFrame error: {}", task_id_, event_name_, e.what());
        throw;
    }
}

void AlgMp4Record::SetTrack(const uint8_t* sps, size_t size) {
    if (size < 4)
        return;

    if (MP4_INVALID_TRACK_ID == track_id_) {
        if (media::VideoCodecType::kH265 == source_type_) {
            track_id_ = MP4AddH265VideoTrack(
                mp4_handle_, timeScale, static_cast<MP4Duration>(timeScale / fps_),
                static_cast<uint16_t>(width_), static_cast<uint16_t>(height_), sps[1], sps[2], sps[3], 3);
        } else {
            track_id_ = MP4AddH264VideoTrack(
                mp4_handle_, timeScale, static_cast<MP4Duration>(timeScale / fps_),
                static_cast<uint16_t>(width_), static_cast<uint16_t>(height_), sps[1], sps[2], sps[3], 3);
        }
        if (MP4_INVALID_TRACK_ID == track_id_) {
            LOG_WARN("[MP4 TASK] {} {} Set Track Failed.", task_id_, event_name_);
            return;
        }

        MP4SetVideoProfileLevel(mp4_handle_, 0x7F);
        MP4SetTrackTimeScale(mp4_handle_, track_id_, static_cast<uint32_t>(timeScale));
    }

    return;
}

std::string AlgMp4Record::GetPath() {
    std::string filePath = cosmo::path::GetEventPath(static_cast<uint64_t>(event_time_));

    return filePath;
}

}  // namespace cosmo
