// VideoDemuxerStream.cc — Stream handling for VideoDemuxer.
// Split from VideoDemuxer.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

#include "media/TimestampMath.h"
#include "media/VideoDemuxer.h"
#include "util/Log.h"
#include "util/RtspUrlUtil.h"
#include "util/TimeUtil.h"

static constexpr const char* kTag = "[DEMUX] ";

namespace cosmo {
namespace media {

    static std::string GetAvErr(int errorNo) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errorNo, buf, AV_ERROR_MAX_STRING_SIZE);
        return buf;
    }

    // Select the best available FPS from stream metadata (avg > codec > r_frame_rate > 25)
    static float SelectBestFps(AVRational avgFps, AVRational codecFps, AVRational streamFps) {
        if (avgFps.num != 0 && avgFps.den != 0) {
            return static_cast<float>(avgFps.num) / static_cast<float>(avgFps.den);
        }
        if (codecFps.num != 0 && codecFps.den != 0) {
            return static_cast<float>(codecFps.num) / static_cast<float>(codecFps.den);
        }
        if (streamFps.num != 0 && streamFps.den != 0) {
            return static_cast<float>(streamFps.num) / static_cast<float>(streamFps.den);
        }
        return 25.0f;
    }

    // find stream...
    util::ErrorEnum VideoDemuxer::FindStream(bool /*bRepeat*/) {
        if (StopRequested()) {
            CloseStream();
            return util::ErrorEnum::FileNotOpened;
        }
        if (!opened_ || !fmt_ctx_) {
            LOG_WARN("{}FindStream {} Stream Not Opened", kTag, util::RedactRtspUrl(filename_));
            return util::ErrorEnum::FileNotOpened;
        }
        // A seek retains valid metadata. A fresh open must be probed even
        // when its caller still carries a pending-repeat flag.
        if (ready_)
            return util::ErrorEnum::Success;
        int ret = 0;
        if ((ret = avformat_find_stream_info(fmt_ctx_, nullptr)) < 0 || StopRequested()) {
            CloseStream();
            LOG_WARN("{}FindStream {} failed.[{}]", kTag, util::RedactRtspUrl(filename_), GetAvErr(ret));
            return util::ErrorEnum::DemuxFindStreamFail;
        }

        AVCodec* codec    = nullptr;
        video_stream_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (video_stream_idx_ < 0 || !codec) {
            const int stream_error = video_stream_idx_ < 0 ? video_stream_idx_ : AVERROR_DECODER_NOT_FOUND;
            CloseStream();
            LOG_WARN("{}FindStream {} stream or codec error.[{}]", kTag, util::RedactRtspUrl(filename_),
                     GetAvErr(stream_error));
            return util::ErrorEnum::DemuxFindVideoStreamFail;
        }

        std::string codec_name = codec->name;
        VideoCodecType next_codec;
        if (codec_name == "h264" || codec_name == "h264_bm") {
            next_codec = VideoCodecType::kH264;
        } else if (codec_name == "hevc" || codec_name == "hevc_bm") {
            next_codec = VideoCodecType::kH265;
        } else if (codec_name == "mjpeg" || codec_name == "mjpg") {
            next_codec = VideoCodecType::kMjpeg;  // USB camera V4L2 MJPEG
        } else {
            CloseStream();
            LOG_WARN("{}FindStream {} codec is not support. [{}]", kTag, util::RedactRtspUrl(filename_),
                     codec_name);
            return util::ErrorEnum::VideoFormatNotSupport;
        }

        AVStream* stream            = fmt_ctx_->streams[video_stream_idx_];
        AVCodecParameters* codecpar = stream->codecpar;

        // Preserve one immutable configuration per stream generation for
        // viewers, decoders and queued recording packets.
        {
            // Preview startup can read headers while this worker reconnects.
            // Publish/copy snapshots rather than returning a mutable reference.
            std::lock_guard<std::mutex> lock(metadata_mtx_);
            enc_type_ = next_codec;
            if (codecpar->extradata && codecpar->extradata_size > 0) {
                extradata_ = std::make_shared<const std::vector<uint8_t>>(
                    codecpar->extradata, codecpar->extradata + codecpar->extradata_size);
            } else {
                extradata_.reset();
            }
        }

        AVRational avgFpsRat    = stream->avg_frame_rate;
        AVRational codecFpsRat  = av_guess_frame_rate(fmt_ctx_, stream, nullptr);
        AVRational streamFpsRat = stream->r_frame_rate;

        width_.store(codecpar->width, std::memory_order_relaxed);
        height_.store(codecpar->height, std::memory_order_relaxed);

        fps_.store(SelectBestFps(avgFpsRat, codecFpsRat, streamFpsRat), std::memory_order_relaxed);

        auto c_fps = fps_.load(std::memory_order_relaxed);

        if (c_fps == 0 || c_fps > 1000) {
            fps_.store(25.0f, std::memory_order_relaxed);
        }

        if (strategy_ && strategy_->NeedsBsf(enc_type_))  // Local MP4 files need bsfc
        {
            if (bsf_ctx_) {
                av_bsf_free(&bsf_ctx_);
                bsf_ctx_ = nullptr;
            }

            if (!InitBsfContext()) {
                LOG_WARN("{}FindStream {} init bsfc Failed", kTag, util::RedactRtspUrl(filename_));
                CloseStream();
                return util::ErrorEnum::DemuxInitBsfcFail;
            }
            // mp4 forced frame rate
            if (video_force_fps_ > 0.0f) {
                fps_.store(video_force_fps_, std::memory_order_relaxed);
            }
        }

        LOG_INFO(
            "{}{} videoStreamIdx:{} codec:{} {}x{} fps_:{} avg[num:{} den:{}], codec[num:{} den:{}], "
            "stream[num:{}, den:{}]",
            kTag, util::RedactRtspUrl(filename_), video_stream_idx_, codec->name, codecpar->width,
            codecpar->height, fps_, avgFpsRat.num, avgFpsRat.den, codecFpsRat.num, codecFpsRat.den,
            streamFpsRat.num, streamFpsRat.den);

        ready_            = true;
        packet_index_     = 0;
        open_time_point_  = std::chrono::steady_clock::now();
        start_time_point_ = open_time_point_;
        return util::ErrorEnum::Success;
    }

    void VideoDemuxer::LocalFileFpsCtrl() {
        std::unique_lock<std::mutex> lockControl(mutex_);
        // Used to control frame rate for local files
        if (strategy_ && strategy_->NeedsFpsControl() && (fps_ > 0.0f)) {
            std::chrono::milliseconds expectTime(
                static_cast<int64_t>(static_cast<float>(packet_index_ * 1000) / fps_));
            auto now      = std::chrono::steady_clock::now();
            auto diffTime = now - open_time_point_;
            if (diffTime < expectTime) {
                // Bound the process-wide cancellation observation latency;
                // per-channel RequestStop additionally wakes this wait.
                while (!StopRequested() && std::chrono::steady_clock::now() - open_time_point_ < expectTime) {
                    const auto remaining = expectTime - (std::chrono::steady_clock::now() - open_time_point_);
                    if (remaining <= std::chrono::steady_clock::duration::zero())
                        break;
                    cond_.wait_for(
                        lockControl,
                        std::min(remaining, std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                std::chrono::milliseconds(50))),
                        [this]() { return StopRequested(); });
                }
            }
        }
    }

    int VideoDemuxer::AvReadFrame(AVPacket* packet) {
        int ret = 0;
        do {
            if (StopRequested())
                return AVERROR_EXIT;
            memset(packet, 0, sizeof(AVPacket));  // Reset structure (prevent dangling pointers)
            av_init_packet(packet);
            ret = av_read_frame(fmt_ctx_, packet);
            if (ret == 0 && (packet->stream_index != video_stream_idx_ || packet->size < 5)) {
                av_packet_unref(packet);
            } else {
                break;
            }
        } while (true);
        if (ret < 0) {
            av_packet_unref(packet);  // Free even on error
        }
        return ret;
    }

    void VideoDemuxer::CalcFps(AVPacket* packet) {
        if (!(packet->flags & AV_PKT_FLAG_KEY))
            return;
        if ((fps_calc_frame_ + 25) >= packet_index_)
            return;

        auto now        = std::chrono::steady_clock::now();
        int64_t ptsDiff = 0;
        if (!TryTimestampDifference(packet->pts, start_pts_, ptsDiff)) {
            start_pts_        = packet->pts;
            start_time_point_ = now;
            fps_calc_frame_   = packet_index_;
            return;
        }

        // 2025-07-16 Added ptsDiff > 0 to prevent positive result
        // on overflow of (packet->pts - m_startPts). A Hikvision camera may output 0x8000000000000000
        // timestamp
        if (ptsDiff > 0) {
            auto ptsCount = packet_index_ - fps_calc_frame_;
            float fpsReceive =
                static_cast<float>(ptsCount) * 1000000.0f /
                static_cast<float>(
                    std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_point_).count());
            int64_t avrageDur           = ptsDiff / ptsCount;
            auto timeBase               = fmt_ctx_->streams[video_stream_idx_]->time_base;
            const bool validDenominator = avrageDur > 0 && timeBase.num > 0 && timeBase.den > 0 &&
                                          avrageDur <= std::numeric_limits<int64_t>::max() / timeBase.num;
            const int64_t denominator = validDenominator ? avrageDur * timeBase.num : 0;
            float fpsNewf =
                validDenominator ? static_cast<float>(timeBase.den) / static_cast<float>(denominator) : 0.0f;
            auto c_fps = fps_.load(std::memory_order_relaxed);
            // Use real-time frame rate if timestamp is incorrect
            if (start_pts_ > packet->pts) {
                fpsNewf = fpsReceive;
            }
            if (fpsNewf > 0 && fpsNewf <= 200 && std::fabs(fpsNewf - c_fps) > 1.0f) {
                LOG_INFO(
                    "{}{} {}x{} fps_: {} -> {}({}) avrageDur:{} num:{} den:{} fpsReceive:[{}] "
                    "ptsCount:{}, duration:{}, packet.pts:{}, startPts:{}",
                    kTag, util::RedactRtspUrl(filename_),
                    fmt_ctx_->streams[video_stream_idx_]->codecpar->width,
                    fmt_ctx_->streams[video_stream_idx_]->codecpar->height, c_fps, fpsNewf, fpsNewf,
                    avrageDur, timeBase.num, timeBase.den, fpsReceive, ptsCount,
                    std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_point_).count(),
                    packet->pts, start_pts_);
                if (strategy_ && strategy_->ShouldUpdateFpsFromPts()) {
                    // RTSP frame rate is more accurate. Local files stay at 25 fps_
                    fps_.store(fpsNewf, std::memory_order_relaxed);
                }
            }
        }

        start_pts_        = packet->pts;
        start_time_point_ = now;
        fps_calc_frame_   = packet_index_;
    }

    void VideoDemuxer::CalcFrameIndex(AVPacket* packet) {
        if (packet->pts == AV_NOPTS_VALUE)
            return;
        if (0 == packet->pts) {
            frame_index_ += 1;
            return;
        }
        // Initialization phase
        if (pts_ <= 0) {
            pts_         = packet->pts;
            frame_index_ = 1;
            return;
        }
        if (frame_index_ < 3) {
            pts_ = packet->pts;
            frame_index_ += 1;
            return;
        }

        int64_t ptsDiff = 0;
        if (!TryTimestampDifference(packet->pts, pts_, ptsDiff)) {
            pts_ = packet->pts;
            return;
        }

        if (ptsDiff == duration_real_time_) {
            duration_active_count_ += 1;
        } else {
            duration_active_count_ = 0;
            if (ptsDiff > 0) {
                duration_real_time_ = ptsDiff;
            }
        }

        // Activate frame duration on first time or three consecutive identical values
        if (duration_active_ < 0 || duration_active_count_ > 3) {
            duration_active_ = duration_real_time_;
        }

        if (ptsDiff <= 0)
            return;

        // If timestamp difference is too large. Too small is considered 1 frame
        if (frame_index_ == std::numeric_limits<int64_t>::max() || duration_active_ <= 0)
            return;
        int64_t frame_number = frame_index_ + 1;
        const auto half      = duration_active_ >> 1;
        const auto threshold = duration_active_ > std::numeric_limits<int64_t>::max() - half
                                   ? std::numeric_limits<int64_t>::max()
                                   : duration_active_ + half;
        if (ptsDiff > threshold) {
            const auto increment = ptsDiff / duration_active_;
            if (increment > std::numeric_limits<int64_t>::max() - frame_index_)
                return;
            frame_number = frame_index_ + increment;
            if (0 == time_diff_count_ % 250) {
                LOG_INFO(
                    "Read frame_number:{} Last frame_number:{} FrameDIFF:{}  pts_:{} ptsDiff:{} "
                    "m_duration:{}, DiffCount:{}",
                    frame_number, frame_index_, frame_number - frame_index_, packet->pts, ptsDiff,
                    duration_active_, time_diff_count_);
            }
            time_diff_count_++;
        }
        pts_         = packet->pts;
        frame_index_ = frame_number;
    }

    ReadFrameStatus VideoDemuxer::Demux(VideoPacketPtr pkt) {
        if (StopRequested() || !ready_ || !fmt_ctx_) {
            return ReadFrameStatus::StreamNotOpen;
        }
        if (end_) {
            return ReadFrameStatus::StreamEnd;
        }
        LocalFileFpsCtrl();
        if (StopRequested())
            return ReadFrameStatus::StreamNotOpen;

        AVPacket packet{};
        int ret = AvReadFrame(&packet);
        //    m_packetIndex++;
        if (0 != ret) {
            LOG_INFO("{}{} get frame error: {} Maybe It's Finished", kTag, util::RedactRtspUrl(filename_),
                     GetAvErr(ret));  // Reached end_ of playback
            end_             = true;
            local_clean_eof_ = ret == AVERROR_EOF;
            pkt->stream_idx  = stream_opened_cnt_;
            av_packet_unref(&packet);
            return ReadFrameStatus::StreamEnd;
        }

        if (packet.size <= 4) {
            LOG_INFO("{}{} empty frame.", kTag, util::RedactRtspUrl(filename_));
            av_packet_unref(&packet);
            return ReadFrameStatus::EmptyFrame;
        }

        if (!key_frame_detected_) {
            key_frame_detected_ = packet.flags & AV_PKT_FLAG_KEY;
            if (!key_frame_detected_) {
                av_packet_unref(&packet);
                return ReadFrameStatus::NotGetIFrame;
            }
            LOG_INFO("{}{} detected key frame.", kTag, util::RedactRtspUrl(filename_));
        }

        // rtsp frame rate may be incorrect, calculate via pts_
        CalcFps(&packet);
        // Some streams have incorrect frame sequence
        CalcFrameIndex(&packet);
        // Get stream fetching timestamp
        auto timestamp = util::GetMilliseconds();
        auto timePoint = std::chrono::steady_clock::now();

        // The bitstream filter consumes its input. Preserve both timestamps and
        // their time base: B-frame display order is not packet/decode order.
        int64_t saved_pts           = packet.pts;
        int64_t saved_dts           = packet.dts;
        int64_t saved_duration      = packet.duration;
        AVRational saved_time_base  = fmt_ctx_->streams[video_stream_idx_]->time_base;
        const bool saved_is_i_frame = static_cast<bool>(packet.flags & AV_PKT_FLAG_KEY);

        // The bitstream filter was initialized with this generation's codec
        // parameters. Do not pass a changed side-data configuration through
        // that old filter or splice its output into an existing publisher.
        if (local_file_source_) {
            for (int i = 0; i < packet.side_data_elems; ++i) {
                const auto& side = packet.side_data[i];
                if (side.type != AV_PKT_DATA_NEW_EXTRADATA)
                    continue;
                bool unchanged = false;
                {
                    std::lock_guard<std::mutex> lock(metadata_mtx_);
                    unchanged = LocalLoopCodecGuard::UnchangedExtradata(extradata_.get(), side.data,
                                                                        static_cast<size_t>(side.size));
                }
                if (!unchanged) {
                    LOG_WARN("{}Local codec side-data changed; ending the current generation", kTag);
                    local_clean_eof_ = false;
                    end_             = true;
                    av_packet_unref(&packet);
                    return ReadFrameStatus::UnsafeLocalContinuity;
                }
            }
        }

        // Video files convert to 0x00000001, rtsp does not
        if (strategy_ && strategy_->NeedsBsf(enc_type_)) {
            if (bsf_ctx_) {
                ret = av_bsf_send_packet(bsf_ctx_, &packet);
                if (ret < 0) {
                    LOG_WARN("{}{} av_bsf_send_packet failed: {}", kTag, util::RedactRtspUrl(filename_),
                             GetAvErr(ret));
                    av_packet_unref(&packet);
                    return ReadFrameStatus::EmptyFrame;
                }

                AVPacket filtered_pkt;
                av_init_packet(&filtered_pkt);
                filtered_pkt.data = nullptr;
                filtered_pkt.size = 0;

                ret = av_bsf_receive_packet(bsf_ctx_, &filtered_pkt);
                if (ret < 0) {
                    LOG_WARN("{}{} av_bsf_receive_packet failed: {}", kTag, util::RedactRtspUrl(filename_),
                             GetAvErr(ret));
                    av_packet_unref(&packet);
                    return ReadFrameStatus::EmptyFrame;
                }

                // Diagnostic: log BSF result for first 3 packets per stream
                if (abs_packet_index_ < 3 && filtered_pkt.size >= 5) {
                    LOG_INFO(
                        "{}{} BSF pkt#{} inSize={} outSize={} bytes:{:02X} {:02X} {:02X} "
                        "{:02X} {:02X}",
                        kTag, util::RedactRtspUrl(filename_), abs_packet_index_, packet.size,
                        filtered_pkt.size, filtered_pkt.data[0], filtered_pkt.data[1], filtered_pkt.data[2],
                        filtered_pkt.data[3], filtered_pkt.data[4]);
                }

                pkt->data = std::vector<uint8_t>(filtered_pkt.data, filtered_pkt.data + filtered_pkt.size);
                saved_pts = filtered_pkt.pts;
                saved_dts = filtered_pkt.dts;
                saved_duration  = filtered_pkt.duration;
                saved_time_base = bsf_ctx_->time_base_out;
                av_packet_unref(&filtered_pkt);
            } else {
                LOG_WARN("{}{} bsf_ctx_ is NULL, skipping BSF", kTag, util::RedactRtspUrl(filename_));
                pkt->data = std::vector<uint8_t>(packet.data, packet.data + packet.size);
            }
        } else {
            pkt->data = std::vector<uint8_t>(packet.data, packet.data + packet.size);
        }

        // Forced read rate changes playback speed. Keep the existing CFR
        // playback clock instead of falsely reporting the original clock.
        const bool forced_file_rate = strategy_ && strategy_->NeedsBsf(enc_type_) && video_force_fps_ > 0.0f;
        if (local_loop_candidate_) {
            const bool boundary = !local_timeline_started_ || local_loop_first_packet_;
            const bool codec_ok =
                video_force_fps_ == local_rate_setting_ &&
                local_codec_guard_.Observe(enc_type_, pkt->data.data(), pkt->data.size(), boundary);
            if (!codec_ok && local_timeline_started_) {
                LOG_WARN("{}Local codec or playback configuration changed; ending the generation", kTag);
                local_clean_eof_ = false;
                end_             = true;
                av_packet_unref(&packet);
                return ReadFrameStatus::UnsafeLocalContinuity;
            }
            LocalLoopTimeline::Prepared prepared;
            std::string reason;
            const LocalLoopInput input{saved_pts,
                                       saved_dts,
                                       saved_duration,
                                       forced_file_rate ? 0 : saved_time_base.num,
                                       forced_file_rate ? 0 : saved_time_base.den,
                                       local_loop_first_packet_,
                                       codec_ok,
                                       forced_file_rate && std::isfinite(fps_.load()) && fps_.load() > 0.0f};
            if (codec_ok && local_timeline_.Prepare(input, prepared, reason) &&
                local_timeline_.Commit(prepared, reason)) {
                saved_pts               = prepared.pts;
                saved_dts               = prepared.dts;
                saved_duration          = prepared.duration;
                saved_time_base         = {prepared.timeBaseNum, prepared.timeBaseDen};
                local_timeline_started_ = true;
                if (prepared.usedDurationFallback)
                    LOG_WARN("{}Local loop seam estimated from preceding DTS interval", kTag);
                local_loop_first_packet_ = false;
            } else {
                // A changed/unknown parameter set or invalid source clock may
                // not be spliced into existing FLV/MP4 output. Publish a new
                // generation and let existing reconnect/finalization handle it.
                if (local_timeline_started_) {
                    LOG_WARN("{}Local timeline cannot continue: {}", kTag, reason);
                    local_clean_eof_ = false;
                    end_             = true;
                    av_packet_unref(&packet);
                    return ReadFrameStatus::UnsafeLocalContinuity;
                }
                local_logical_stream_    = stream_opened_cnt_;
                local_loop_candidate_    = false;
                local_loop_first_packet_ = false;
                LOG_WARN("{}Local loop continuity unavailable: {}", kTag,
                         codec_ok ? reason : "codec parameters or random access not proven");
            }
        }
        packet_index_++;
        abs_packet_index_++;
        pkt->pts           = saved_pts;
        pkt->dts           = saved_dts;
        pkt->duration      = saved_duration;
        pkt->time_base_num = forced_file_rate ? 0 : saved_time_base.num;
        pkt->time_base_den = forced_file_rate ? 0 : saved_time_base.den;
        pkt->timestamp     = timestamp;
        pkt->timestamp_epoch =
            std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count();
        pkt->time_point = timePoint;
        pkt->stream_idx = local_loop_candidate_ ? local_logical_stream_ : stream_opened_cnt_;
        pkt->index      = local_loop_candidate_ ? abs_packet_index_ : packet_index_;
        pkt->abs_idx    = abs_packet_index_;
        pkt->is_i_frame = saved_is_i_frame;
        {
            std::lock_guard<std::mutex> lock(metadata_mtx_);
            pkt->codec_type      = enc_type_;
            pkt->codec_extradata = extradata_;
        }
        pkt->width  = static_cast<size_t>(width_.load());
        pkt->height = static_cast<size_t>(height_.load());
        pkt->fps    = fps_;

        // NOTE: Intentionally ignoring dynamic resolution changes here to prevent pipeline crashes.

        av_packet_unref(&packet);
        return ReadFrameStatus::Success;
    }

    void VideoDemuxer::SafeCloseContext() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (fmt_ctx_) {
            avformat_close_input(&fmt_ctx_);
            fmt_ctx_ = nullptr;
            LOG_INFO("{}Stream {} fmt_ctx_ Closed.", kTag, util::RedactRtspUrl(filename_));
        }
    }

    bool VideoDemuxer::InitBsfContext() {
        const char* bsf_name = nullptr;
        switch (enc_type_) {
            case VideoCodecType::kH264:
                bsf_name = "h264_mp4toannexb";
                break;
            case VideoCodecType::kH265:
                bsf_name = "hevc_mp4toannexb";
                break;
            default:
                return false;
        }

        const AVBitStreamFilter* filter = av_bsf_get_by_name(bsf_name);
        if (!filter) {
            LOG_WARN("{}{} BSF '{}' not found", kTag, util::RedactRtspUrl(filename_), bsf_name);
            return false;
        }

        int ret = av_bsf_alloc(filter, &bsf_ctx_);
        if (ret < 0) {
            LOG_WARN("{}{} av_bsf_alloc failed: {}", kTag, util::RedactRtspUrl(filename_), GetAvErr(ret));
            return false;
        }

        // Copy codec parameters to BSF context input
        ret = avcodec_parameters_copy(bsf_ctx_->par_in, fmt_ctx_->streams[video_stream_idx_]->codecpar);
        if (ret < 0) {
            LOG_WARN("{}{} avcodec_parameters_copy failed: {}", kTag, util::RedactRtspUrl(filename_),
                     GetAvErr(ret));
            av_bsf_free(&bsf_ctx_);
            return false;
        }

        bsf_ctx_->time_base_in = fmt_ctx_->streams[video_stream_idx_]->time_base;
        ret                    = av_bsf_init(bsf_ctx_);
        if (ret < 0) {
            LOG_WARN("{}{} av_bsf_init failed: {}", kTag, util::RedactRtspUrl(filename_), GetAvErr(ret));
            av_bsf_free(&bsf_ctx_);
            return false;
        }

        LOG_INFO("{}{} BSF '{}' initialized successfully", kTag, util::RedactRtspUrl(filename_), bsf_name);
        return true;
    }

}  // namespace media
}  // namespace cosmo
