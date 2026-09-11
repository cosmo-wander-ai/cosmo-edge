// AlgMp4Record — Annex-B conversion and synchronous MP4 sample writing.

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>

#include "flow/channel/AlgMp4Record.h"
#include "mp4v2/mp4v2.h"
#include "mp4v2/track.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo {
namespace {
    constexpr uint32_t kTimeScale    = 90000;
    constexpr size_t kNaluLengthSize = 4;

    struct NaluView {
        const uint8_t* data{nullptr};
        size_t size{0};
    };

    // Walk the input without owning it. Zero runs before a start code and at the
    // end are Annex-B padding; emulation-prevention bytes remain untouched.
    template <typename Visitor>
    bool WalkAnnexB(const uint8_t* data, size_t size, Visitor visit) {
        if (!data || size == 0) {
            return false;
        }
        size_t pos = 0;
        while (pos < size && data[pos] == 0) {
            ++pos;
        }
        if (pos < 2 || pos == size || data[pos] != 1) {
            return false;
        }
        size_t begin = ++pos;
        size_t zeros = 0;
        for (; pos < size; ++pos) {
            if (data[pos] == 0) {
                ++zeros;
                continue;
            }
            if (data[pos] == 1 && zeros >= 2) {
                const size_t end = pos - zeros;
                if (end == begin || !visit(NaluView{data + begin, end - begin})) {
                    return false;
                }
                begin = pos + 1;
            }
            zeros = 0;
        }
        const size_t end = size - zeros;
        return end > begin && visit(NaluView{data + begin, end - begin});
    }

    int NaluType(media::VideoCodecType codec, const NaluView& nalu) {
        return codec == media::VideoCodecType::kH265 ? (nalu.data[0] >> 1) & 0x3f : nalu.data[0] & 0x1f;
    }

    bool IsParameterSet(media::VideoCodecType codec, int type) {
        return codec == media::VideoCodecType::kH265 ? type >= 32 && type <= 34 : type == 7 || type == 8;
    }
}  // namespace

struct AlgMp4Record::FrameScan {
    size_t output_size{0};
    bool has_video{false};
    bool is_sync{true};
    NaluView vps;
    NaluView sps;
    NaluView pps;
};

bool AlgMp4Record::ScanFrame(const uint8_t* data, size_t size, FrameScan* scan) const {
    const bool hevc = source_type_ == media::VideoCodecType::kH265;
    if (!hevc && source_type_ != media::VideoCodecType::kH264) {
        return false;
    }
    return WalkAnnexB(data, size, [&](const NaluView& nalu) {
        if ((nalu.data[0] & 0x80) != 0 || (hevc && (nalu.size < 2 || (nalu.data[1] & 7) == 0))) {
            return false;
        }
        const int type = NaluType(source_type_, nalu);
        if (!hevc && type == 0) {
            return false;
        }
        if (IsParameterSet(source_type_, type)) {
            const bool is_pps = hevc ? type == 34 : type == 8;
            // SetTrack reads the first four bytes of VPS/SPS.
            const size_t minimum = is_pps ? (hevc ? 2 : 1) : 4;
            if (nalu.size < minimum || nalu.size > std::numeric_limits<uint16_t>::max()) {
                return false;
            }
            NaluView* parameter = is_pps ? &scan->pps : (hevc && type == 32 ? &scan->vps : &scan->sps);
            if (!parameter->data) {
                *parameter = nalu;
            }
            return true;
        }
        const size_t limit =
            std::min(static_cast<size_t>(std::numeric_limits<uint32_t>::max()), sample_buffer_.max_size());
        if (scan->output_size > limit || limit - scan->output_size < kNaluLengthSize ||
            nalu.size > limit - scan->output_size - kNaluLengthSize) {
            return false;
        }
        scan->output_size += kNaluLengthSize + nalu.size;
        const bool is_video = hevc ? type <= 31 : (type >= 1 && type <= 5) || (type >= 19 && type <= 21);
        if (is_video) {
            scan->has_video = true;
            scan->is_sync   = scan->is_sync && (hevc ? type >= 16 && type <= 21 : type == 5);
        }
        return true;
    });
}

bool AlgMp4Record::TransformFrame(const uint8_t* data, size_t size, const FrameScan& scan) {
    try {
        sample_buffer_.resize(scan.output_size);
        size_t offset    = 0;
        const bool valid = WalkAnnexB(data, size, [&](const NaluView& nalu) {
            if (IsParameterSet(source_type_, NaluType(source_type_, nalu))) {
                return true;
            }
            if (offset > sample_buffer_.size() || sample_buffer_.size() - offset < kNaluLengthSize ||
                nalu.size > sample_buffer_.size() - offset - kNaluLengthSize) {
                return false;
            }
            const auto length = static_cast<uint32_t>(nalu.size);
            for (size_t i = 0; i < kNaluLengthSize; ++i) {
                sample_buffer_[offset + i] = static_cast<uint8_t>(length >> ((kNaluLengthSize - 1 - i) * 8));
            }
            std::memcpy(sample_buffer_.data() + offset + kNaluLengthSize, nalu.data, nalu.size);
            offset += kNaluLengthSize + nalu.size;
            return true;
        });
        if (valid && offset == scan.output_size) {
            return true;
        }
    } catch (const std::exception& e) {
        LOG_WARN("[MP4 TASK] {} {} TransformFrame failed: {}", task_id_, event_name_, e.what());
    }
    sample_buffer_.clear();
    return false;
}

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
    if (media::VideoCodecType::kH265 == source_type_) {
        MP4AddH265PictureParameterSet(mp4_handle_, track_id_, frame_data, static_cast<uint16_t>(frame_size));
    } else {
        MP4AddH264PictureParameterSet(mp4_handle_, track_id_, frame_data, static_cast<uint16_t>(frame_size));
    }
    is_pps_ready_ = true;
    return false;
}

bool AlgMp4Record::WriteVideoSample(int64_t seq, bool is_sync) {
    try {
        const bool written = MP4WriteSample(mp4_handle_, track_id_, sample_buffer_.data(),
                                            static_cast<uint32_t>(sample_buffer_.size()),
                                            static_cast<MP4Duration>(kTimeScale / fps_), 0, is_sync);
        const auto samples = MP4GetTrackNumberOfSamples(mp4_handle_, track_id_);
        if (written && static_cast<int64_t>(samples) == record_frames_ + 1) {
            ++record_frames_;
            total_size_ += static_cast<int64_t>(sample_buffer_.size());
            return true;
        }
        LOG_ERRO("[MP4 TASK] {} {} Frame:{} write failed: result={} samples={} expected={}", task_id_,
                 event_name_, seq, written, samples, record_frames_ + 1);
    } catch (const std::exception& e) {
        LOG_ERRO("[MP4 TASK] {} {} Frame:{} MP4WriteSample error:{}", task_id_, event_name_, seq, e.what());
    } catch (...) {
        LOG_ERRO("[MP4 TASK] {} {} Frame:{} MP4WriteSample unknown error", task_id_, event_name_, seq);
    }
    write_failed_ = true;
    sample_buffer_.clear();
    return false;
}

bool AlgMp4Record::RecodeFrame(VideoPacketPtr frame) {
    sample_buffer_.clear();
    if (write_failed_ || !frame || !mp4_handle_) {
        return false;
    }
    if (frame->stream_idx != stream_index_) {
        ++stream_mismatch_count_;
        if (stream_mismatch_count_ == 1 || stream_mismatch_count_ % 100 == 0) {
            LOG_WARN("[MP4 TASK] {} {} Stream Mismatch (x{}): Frame={} FrameStream={} expect={}, skip.",
                     task_id_, event_name_, stream_mismatch_count_, frame->GetSequence(), frame->stream_idx,
                     stream_index_);
        }
        return false;
    }
    if (!frame->GetData() || frame->GetSize() == 0 || frame->GetSize() > max_frame_size_) {
        LOG_WARN("[MP4 TASK] {} {} Record Frame:{}, dataSize:{}. MaxSize:{}", task_id_, event_name_,
                 frame->GetSequence(), frame->GetSize(), max_frame_size_);
        return false;
    }
    data_size_ += static_cast<int64_t>(frame->GetSize());
    FrameScan scan;
    if (!ScanFrame(frame->GetData(), frame->GetSize(), &scan) ||
        !TransformFrame(frame->GetData(), frame->GetSize(), scan)) {
        LOG_WARN("[MP4 TASK] {} {} Frame:{} invalid Annex-B input", task_id_, event_name_,
                 frame->GetSequence());
        return false;
    }
    const int64_t seq = frame->GetSequence();
    if (last_index_ == -1) {
        start_index_ = seq;
        start_time_  = frame->GetTimestamp();
    }
    // This is an input cursor, including valid parameter-only packets.
    last_index_ = seq;
    last_time_  = frame->GetTimestamp();
    if (scan.vps.data) {
        HandleVps(scan.vps.data, scan.vps.size, seq, scan.is_sync && scan.has_video);
    }
    if (scan.sps.data) {
        HandleSps(scan.sps.data, scan.sps.size, seq, scan.is_sync && scan.has_video);
    }
    if (scan.pps.data) {
        HandlePps(scan.pps.data, scan.pps.size, seq, scan.is_sync && scan.has_video);
    }
    if (!scan.has_video || track_id_ == MP4_INVALID_TRACK_ID || !is_sps_ready_ || !is_pps_ready_ ||
        (source_type_ == media::VideoCodecType::kH265 && !is_vps_ready_)) {
        sample_buffer_.clear();
        return false;
    }
    return WriteVideoSample(seq, scan.is_sync);
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

void AlgMp4Record::SetTrack(const uint8_t* sps, size_t size) {
    if (size < 4)
        return;

    if (MP4_INVALID_TRACK_ID == track_id_) {
        if (media::VideoCodecType::kH265 == source_type_) {
            track_id_ = MP4AddH265VideoTrack(
                mp4_handle_, kTimeScale, static_cast<MP4Duration>(kTimeScale / fps_),
                static_cast<uint16_t>(width_), static_cast<uint16_t>(height_), sps[1], sps[2], sps[3], 3);
        } else {
            track_id_ = MP4AddH264VideoTrack(
                mp4_handle_, kTimeScale, static_cast<MP4Duration>(kTimeScale / fps_),
                static_cast<uint16_t>(width_), static_cast<uint16_t>(height_), sps[1], sps[2], sps[3], 3);
        }
        if (MP4_INVALID_TRACK_ID == track_id_) {
            LOG_WARN("[MP4 TASK] {} {} Set Track Failed.", task_id_, event_name_);
            return;
        }

        MP4SetVideoProfileLevel(mp4_handle_, 0x7F);
        MP4SetTrackTimeScale(mp4_handle_, track_id_, static_cast<uint32_t>(kTimeScale));
    }

    return;
}

std::string AlgMp4Record::GetPath() {
    std::string filePath = cosmo::path::GetEventPath(static_cast<uint64_t>(event_time_));

    return filePath;
}

}  // namespace cosmo
