// RtmpStreamPusher — Rtmp Stream Pusher implementation.

#include "flow/stream/RtmpStreamPusher.h"

#include <algorithm>

#include "util/FormatString.h"
#include "util/Log.h"

namespace cosmo {

namespace {
    std::string GetAvErr(int errorNo) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errorNo, buf, AV_ERROR_MAX_STRING_SIZE);
        return buf;
    }

    size_t MinFirstKeyFrameSize(int width, int height) {
        const auto pixels =
            static_cast<size_t>(std::max(width, 1)) * static_cast<size_t>(std::max(height, 1));
        return std::max<size_t>(1024, pixels / 4096);
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
        throw util::Exception(COSMO_FORMAT("{}", GetAvErr(ret)));
    }
}

RtmpStreamPusher::~RtmpStreamPusher() {
    LOG_INFO("{}", "closing RTMP stream pusher");
    CloseOutput();
    LOG_INFO("{}", "RTMP stream pusher closed");
}

bool RtmpStreamPusher::WaitReady(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(ready_mtx_);
    return ready_cv_.wait_for(lock, timeout, [this] { return stream_ready_; });
}

int RtmpStreamPusher::InitOutput() {
    int ret = avformat_alloc_output_context2(&outctx_, nullptr, "flv", nullptr);
    if (ret < 0) {
        LOG_ERRO("avformat_alloc_output_context2 failed: [{}]", GetAvErr(ret));
        return ret;
    }

    outstream_ = avformat_new_stream(outctx_, nullptr);
    if (!outstream_) {
        ret = AVERROR(errno);
        LOG_ERRO("avformat_new_stream failed: [{}]", GetAvErr(ret));
        CloseOutput();
        return ret;
    }

    ret = avio_open(&outctx_->pb, push_url_.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0 || !outctx_->pb) {
        LOG_ERRO("avio_open failed: [{}] url={}", GetAvErr(ret), push_url_);
        CloseOutput();
        return ret;
    }

    LOG_INFO("RTMP output initialized: url={}", push_url_);
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
}

bool RtmpStreamPusher::ReopenOutput() {
    CloseOutput();
    stream_ready_  = false;
    output_failed_ = false;
    first_frame_flag_.clear();
    packet_writer_->ResetCounter();

    int ret = InitOutput();
    if (ret < 0) {
        LOG_ERRO("reopen RTMP output failed: [{}]", GetAvErr(ret));
        output_failed_ = true;
        return false;
    }

    LOG_INFO("reopen RTMP output success: {}", push_url_);
    return true;
}

void RtmpStreamPusher::PushHeader() {
    if (!outctx_ || !outstream_) {
        return;
    }

    codec_config_->ConfigureStream(outstream_);
    outindex_ = outstream_->id;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "fflags", "nobuffer", 0);
    av_dict_set(&options, "max_delay", "100000", 0);

    int ret = avformat_write_header(outctx_, &options);
    av_dict_free(&options);

    if (ret < 0) {
        LOG_ERRO("avformat_write_header failed: [{}] url={}", GetAvErr(ret), push_url_);
        output_failed_ = true;
        CloseOutput();
        first_frame_flag_.clear();
    } else {
        LOG_INFO("{}", "avformat_write_header success");
    }
}

void RtmpStreamPusher::DoPushFrame(const uint8_t* data, size_t size) {
    debug_info_.recvFrames += 1;

    // Diagnostic: log frame header bytes for first 5 frames
    if (debug_info_.recvFrames <= 5 && size >= 5) {
        LOG_INFO("DoPushFrame #{} size:{} bytes:{:02X} {:02X} {:02X} {:02X} {:02X}", debug_info_.recvFrames,
                 size, data[0], data[1], data[2], data[3], data[4]);
    }

    if (size == 0) {
        LOG_WARN("{}", "DoPushFrame: empty frame");
        return;
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

    // Wait for I-frame during error recovery
    if (output_failed_) {
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
        const size_t min_key_frame_size = MinFirstKeyFrameSize(width_, height_);
        if (out_size < min_key_frame_size) {
            first_frame_flag_.clear();
            if (skip_count_++ <= 3) {
                LOG_WARN("DoPushFrame skip: first I-frame too small size:{} min:{} at frame #{}", out_size,
                         min_key_frame_size, debug_info_.recvFrames);
            }
            return;
        }

        LOG_INFO("{}", "first I-frame received, pushing header");
        PushHeader();
    }

    // Only push I and P frames
    if (main_type == media::HFrameType::I || main_type == media::HFrameType::P) {
        if (first_frame_flag_.test_and_set()) {
            bool is_key_frame = (main_type == media::HFrameType::I);
            int ret = packet_writer_->WriteFrame(outctx_, outindex_, out_data, out_size, is_key_frame);
            if (ret < 0) {
                LOG_ERRO("WriteFrame failed, url={}", push_url_);
                output_failed_ = true;
                CloseOutput();
                first_frame_flag_.clear();
            } else {
                debug_info_.sendFrames += 1;
                if (!stream_ready_) {
                    {
                        std::lock_guard<std::mutex> lock(ready_mtx_);
                        stream_ready_ = true;
                    }
                    ready_cv_.notify_all();
                    LOG_INFO("{}", "Stream ready: first frame written to RTMP/SRS");
                }
                if (outctx_ && outctx_->pb) {
                    avio_flush(outctx_->pb);
                }
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
