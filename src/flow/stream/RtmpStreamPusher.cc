// RtmpStreamPusher — Rtmp Stream Pusher implementation.

#include "flow/stream/RtmpStreamPusher.h"

#include "media/PreviewPipelineMetrics.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/FormatString.h"
#include "util/Log.h"

namespace cosmo {

namespace {
    std::string GetAvErr(int errorNo) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errorNo, buf, AV_ERROR_MAX_STRING_SIZE);
        return buf;
    }
}  // namespace

RtmpStreamPusher::RtmpStreamPusher(media::VideoCodecType origin_type, const std::string& url, int width,
                                   int height, float fps)
    : push_url_(url), codec_type_(origin_type) {
    if ((width > 1920) || (height > 1088)) {
        // In case of 400w input, decode to 1080P. Reference: CopyYUV420SPLimitArea
        LOG_WARN("Push Src is {}x{} But Max Support is 1080P Modify To 1080P.", width, height);
        width  = 1920;
        height = 1080;
    }
    LOG_INFO("RTMP push configured, width:{}, height:{}, fps:{}", width, height, fps);
    SetVideoAttr(width, height, fps);

    codec_config_  = std::make_unique<RtmpCodecConfig>(origin_type, width_, height_, fps);
    packet_writer_ = std::make_unique<RtmpPacketWriter>(fps);
    LOG_INFO("camera fps: {}", fps);

    int ret = InitOutput();
    if (ret < 0) {
        throw util::ErrorMessage(util::make_error_condition(util::ErrorEnum::LiveStreamPublishFailed),
                                 COSMO_FORMAT("RTMP publisher connect failed: {}", GetAvErr(ret)).c_str());
    }
    publisher_registered_ = true;
    media::GetPreviewPipelineMetrics().PublisherOpened();
}

RtmpStreamPusher::~RtmpStreamPusher() {
    Stop();
}

void RtmpStreamPusher::SetVideoAttr(int width, int height, float fps) {
    std::lock_guard<std::mutex> lock(output_mtx_);
    SetVideoAttrLocked(width, height, fps);
}

void RtmpStreamPusher::SetVideoAttrLocked(int width, int height, float fps) {
    if ((width < 64) || (height < 64) || (fps < 0.1f))
        return;
    width_  = width;
    height_ = height;
    fps_    = fps;
}

bool RtmpStreamPusher::WaitReady(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(ready_mtx_);
    const bool state_changed = ready_cv_.wait_for(
        lock, timeout, [this] { return stream_ready_ || stopping_.load(std::memory_order_acquire); });
    return state_changed && stream_ready_;
}

void RtmpStreamPusher::SetCodecParamsFromExtradata(const std::vector<uint8_t>& extradata) {
    if (extradata.empty() || stopping_.load(std::memory_order_acquire)) {
        return;
    }
    std::lock_guard<std::mutex> lock(output_mtx_);
    if (!stopping_.load(std::memory_order_relaxed)) {
        codec_config_->SetParameters(extradata);
    }
}

bool RtmpStreamPusher::SetStreamReady(bool ready) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(ready_mtx_);
        changed       = stream_ready_ != ready;
        stream_ready_ = ready;
        if (ready) {
            last_error_.clear();
        }
    }
    if (changed) {
        ready_cv_.notify_all();
    }
    return changed;
}

bool RtmpStreamPusher::IsReady() const {
    std::lock_guard<std::mutex> lock(ready_mtx_);
    return stream_ready_ && !stopping_.load(std::memory_order_acquire) && !output_failed_.load();
}

void RtmpStreamPusher::Stop() {
    if (stopping_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    LOG_INFO("{}", "RTMP publisher stopping");
    {
        std::lock_guard<std::mutex> lock(output_mtx_);
        CloseOutput();
    }
    if (publisher_registered_.exchange(false)) {
        media::GetPreviewPipelineMetrics().PublisherClosed();
    }
    ready_cv_.notify_all();
    LOG_INFO("{}", "RTMP publisher stopped and released");
}

std::string RtmpStreamPusher::LastError() const {
    std::lock_guard<std::mutex> lock(ready_mtx_);
    return last_error_;
}

void RtmpStreamPusher::RecordFailure(const char* stage, int error_no) {
    const std::string detail = COSMO_FORMAT("{}: {}", stage, GetAvErr(error_no));
    {
        std::lock_guard<std::mutex> lock(ready_mtx_);
        last_error_   = detail;
        stream_ready_ = false;
    }
    ready_cv_.notify_all();
    LOG_ERRO("RTMP publisher failure: stage={} error={}", stage, GetAvErr(error_no));
}

int RtmpStreamPusher::InitOutput() {
    int ret = avformat_alloc_output_context2(&outctx_, nullptr, "flv", nullptr);
    if (ret < 0) {
        LOG_ERRO("avformat_alloc_output_context2 failed: [{}]", GetAvErr(ret));
        return ret;
    }

    outstream_ = avformat_new_stream(outctx_, nullptr);
    if (!outstream_) {
        ret = AVERROR(ENOMEM);
        LOG_ERRO("avformat_new_stream failed: [{}]", GetAvErr(ret));
        CloseOutput();
        return ret;
    }

    ret = avio_open(&outctx_->pb, push_url_.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0 || !outctx_->pb) {
        if (ret >= 0) {
            ret = AVERROR(EIO);
        }
        RecordFailure("connect", ret);
        CloseOutput();
        return ret;
    }

    LOG_INFO("{}", "RTMP output initialized");
    return 0;
}

void RtmpStreamPusher::CloseOutput() {
    if (outstream_ && outstream_->codecpar) {
        av_freep(&outstream_->codecpar->extradata);
        outstream_->codecpar->extradata_size = 0;
    }
    if (outctx_) {
        if (outctx_->pb) {
            avio_closep(&outctx_->pb);
        }
        avformat_free_context(outctx_);
    }
    outctx_         = nullptr;
    outstream_      = nullptr;
    outindex_       = 0;
    header_written_ = false;
    SetStreamReady(false);
}

bool RtmpStreamPusher::ReopenOutput() {
    CloseOutput();
    output_failed_.store(false);
    packet_writer_->ResetCounter();

    int ret = InitOutput();
    if (ret < 0) {
        LOG_ERRO("reopen RTMP output failed: [{}]", GetAvErr(ret));
        output_failed_.store(true);
        return false;
    }

    LOG_INFO("{}", "reopen RTMP output success");
    return true;
}

bool RtmpStreamPusher::PushHeader() {
    if (!outctx_ || !outstream_) {
        return false;
    }

    codec_config_->ConfigureStream(outstream_);
    outindex_ = outstream_->id;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "fflags", "nobuffer", 0);
    av_dict_set(&options, "max_delay", "100000", 0);

    int ret = avformat_write_header(outctx_, &options);
    av_dict_free(&options);

    if (ret < 0) {
        RecordFailure("write-header", ret);
        output_failed_.store(true);
        CloseOutput();
        return false;
    } else {
        LOG_INFO("{}", "avformat_write_header success");
    }
    return true;
}

void RtmpStreamPusher::PushFrame(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        LOG_WARN("{}", "PushFrame: empty frame");
        return;
    }
    if (stopping_.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> output_lock(output_mtx_);
    if (stopping_.load(std::memory_order_relaxed)) {
        return;
    }
    PushFrameLocked(data, size);
}

void RtmpStreamPusher::PushPacket(const media::VideoPacket& packet) {
    if (packet.data.empty() || stopping_.load(std::memory_order_acquire))
        return;
    std::lock_guard<std::mutex> output_lock(output_mtx_);
    if (stopping_.load(std::memory_order_relaxed))
        return;
    if (packet.codec_type != codec_type_) {
        LOG_WARN("{}", "Raw preview codec changed; viewer must be recreated");
        CloseOutput();
        output_failed_.store(true);
        return;
    }
    if (!raw_stream_index_ || *raw_stream_index_ != packet.stream_idx) {
        const bool reconnect = raw_stream_index_.has_value();
        raw_stream_index_    = packet.stream_idx;
        SetVideoAttrLocked(packet.GetWidth(), packet.GetHeight(), packet.GetFPS());
        // Never reuse the preceding stream's parameter sets after reconnect,
        // including when the new stream has absent or malformed extradata.
        codec_config_  = std::make_unique<RtmpCodecConfig>(codec_type_, width_, height_, fps_);
        packet_writer_ = std::make_unique<RtmpPacketWriter>(fps_);
        if (packet.codec_extradata && !packet.codec_extradata->empty())
            codec_config_->SetParameters(*packet.codec_extradata);
        if (reconnect) {
            CloseOutput();
            output_failed_.store(true);
        }
        LOG_INFO("Raw preview stream generation={} reconnect={} codecParameters={}", packet.stream_idx,
                 reconnect, codec_config_->HasParameters());
    }
    PushFrameLocked(packet.data.data(), packet.data.size(), &packet);
}

void RtmpStreamPusher::PushFrameLocked(const uint8_t* data, size_t size, const media::VideoPacket* source) {
    const auto publish_started_at = std::chrono::steady_clock::now();
    debug_info_.recvFrames += 1;

    // Diagnostic: log frame header bytes for first 5 frames
    if (debug_info_.recvFrames <= 5 && size >= 5) {
        LOG_INFO("PushFrame #{} size:{} bytes:{:02X} {:02X} {:02X} {:02X} {:02X}", debug_info_.recvFrames,
                 size, data[0], data[1], data[2], data[3], data[4]);
    }

    // Parse NALUs, extract codec params, and prepare output data
    const uint8_t* out_data = nullptr;
    size_t out_size         = 0;
    media::HFrameType main_type{};
    media::HFrameType lead_type{};
    const bool has_video_slice =
        codec_config_->ParseAndPrepare(data, size, out_data, out_size, main_type, lead_type);

    if (!has_video_slice) {
        if (skip_count_++ <= 3) {
            LOG_WARN("PushFrame skip: parameter-only/unknown packet leadType={} size={} at frame #{}",
                     static_cast<int>(lead_type), size, debug_info_.recvFrames);
        }
        return;
    }
    if (!out_data || out_size == 0) {
        LOG_WARN("PushFrame skip: parser returned empty video payload at frame #{}", debug_info_.recvFrames);
        return;
    }

    // Neither a fresh connection nor a reconnect may publish an unconfigured IDR.
    if (main_type == media::HFrameType::I && !codec_config_->HasParameters()) {
        if (skip_count_++ <= 3) {
            LOG_WARN("PushFrame skip: I-frame before codec parameters at frame #{}", debug_info_.recvFrames);
        }
        return;
    }

    // Wait for a parameterized I-frame during error recovery or source reconnect.
    if (output_failed_.load()) {
        if (main_type != media::HFrameType::I) {
            return;
        }
        if (!ReopenOutput()) {
            return;
        }
    }

    // A preceding P-frame must not mark the header as written.
    if (!header_written_) {
        if (main_type != media::HFrameType::I)
            return;
        LOG_INFO("first I-frame received, pushing header: size={}", out_size);
        if (!PushHeader()) {
            return;
        }
        header_written_ = true;
    }

    // Only push I and P frames
    if (main_type == media::HFrameType::I || main_type == media::HFrameType::P) {
        if (header_written_) {
            bool is_key_frame = (main_type == media::HFrameType::I);
            int ret =
                packet_writer_->WriteFrame(outctx_, outindex_, out_data, out_size, is_key_frame, source);
            if (ret < 0) {
                RecordFailure("write-frame", ret);
                output_failed_.store(true);
                CloseOutput();
            } else {
                debug_info_.sendFrames += 1;
                if (SetStreamReady(true)) {
                    LOG_INFO("{}", "Stream ready: first frame written to RTMP/SRS");
                }
                if (outctx_ && outctx_->pb) {
                    avio_flush(outctx_->pb);
                }
                media::GetPreviewPipelineMetrics().RecordPublishedFrame(
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - publish_started_at)
                                              .count()));
            }
        }
    } else if (skip_count_++ <= 3) {
        LOG_WARN("PushFrame skip: frameMainType={} (not I/P) at frame #{}", static_cast<int>(main_type),
                 debug_info_.recvFrames);
    }
}

}  // namespace cosmo
