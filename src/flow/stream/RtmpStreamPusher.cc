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
    : VideoStreamPush(std::move(url), width, height, fps),
      codec_config_(std::make_unique<RtmpCodecConfig>(origin_type, width_, height_, fps)),
      packet_writer_(std::make_unique<RtmpPacketWriter>(fps)),
      first_frame_flag_(ATOMIC_FLAG_INIT) {
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
    outctx_    = nullptr;
    outstream_ = nullptr;
    outindex_  = 0;
    SetStreamReady(false);
}

bool RtmpStreamPusher::ReopenOutput() {
    CloseOutput();
    output_failed_.store(false);
    first_frame_flag_.clear();
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
        first_frame_flag_.clear();
        return false;
    } else {
        LOG_INFO("{}", "avformat_write_header success");
    }
    return true;
}

void RtmpStreamPusher::DoPushFrame(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        LOG_WARN("{}", "DoPushFrame: empty frame");
        return;
    }
    if (stopping_.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> output_lock(output_mtx_);
    if (stopping_.load(std::memory_order_relaxed)) {
        return;
    }
    const auto publish_started_at = std::chrono::steady_clock::now();
    debug_info_.recvFrames += 1;

    // Diagnostic: log frame header bytes for first 5 frames
    if (debug_info_.recvFrames <= 5 && size >= 5) {
        LOG_INFO("DoPushFrame #{} size:{} bytes:{:02X} {:02X} {:02X} {:02X} {:02X}", debug_info_.recvFrames,
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
            LOG_WARN("DoPushFrame skip: parameter-only/unknown packet leadType={} size={} at frame #{}",
                     static_cast<int>(lead_type), size, debug_info_.recvFrames);
        }
        return;
    }
    if (!out_data || out_size == 0) {
        LOG_WARN("DoPushFrame skip: parser returned empty video payload at frame #{}",
                 debug_info_.recvFrames);
        return;
    }

    // Wait for I-frame during error recovery
    if (output_failed_.load()) {
        if (main_type != media::HFrameType::I) {
            return;
        }
        if (!ReopenOutput()) {
            return;
        }
    }

    // Write header on first I-frame
    if (main_type == media::HFrameType::I && !codec_config_->HasParameters()) {
        if (skip_count_++ <= 3) {
            LOG_WARN("DoPushFrame skip: I-frame before codec parameters at frame #{}",
                     debug_info_.recvFrames);
        }
        return;
    }

    if (main_type == media::HFrameType::I && !first_frame_flag_.test_and_set()) {
        LOG_INFO("first I-frame received, pushing header: size={}", out_size);
        if (!PushHeader()) {
            return;
        }
    }

    // Only push I and P frames
    if (main_type == media::HFrameType::I || main_type == media::HFrameType::P) {
        if (first_frame_flag_.test_and_set()) {
            bool is_key_frame = (main_type == media::HFrameType::I);
            int ret = packet_writer_->WriteFrame(outctx_, outindex_, out_data, out_size, is_key_frame);
            if (ret < 0) {
                RecordFailure("write-frame", ret);
                output_failed_.store(true);
                CloseOutput();
                first_frame_flag_.clear();
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
        } else {
            first_frame_flag_.clear();
        }
    } else if (skip_count_++ <= 3) {
        LOG_WARN("DoPushFrame skip: frameMainType={} (not I/P) at frame #{}", static_cast<int>(main_type),
                 debug_info_.recvFrames);
    }
}

}  // namespace cosmo
