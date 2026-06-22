// VideoDemuxerStream.cc — Stream handling for VideoDemuxer.
// Split from VideoDemuxer.cc to reduce file size (DEBT-007).

#include <thread>

#include "media/VideoDemuxer.h"
#include "util/Log.h"
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
    util::ErrorEnum VideoDemuxer::FindStream(bool bRepeat) {
        // Looping playback requires VoD file
        if (bRepeat && strategy_ && strategy_->SupportsRepeat()) {
            return util::ErrorEnum::Success;
        }

        if (!opened_) {
            LOG_WARN("{}FindStream {} Stream Not Opened", kTag, filename_);
            return util::ErrorEnum::FileNotOpened;
        }
        int ret = 0;
        if ((ret = avformat_find_stream_info(fmt_ctx_, nullptr)) != 0) {
            SafeCloseContext();
            LOG_WARN("{}FindStream {} failed.[{}]", kTag, filename_, GetAvErr(ret));
            return util::ErrorEnum::DemuxFindStreamFail;
        }

        AVCodec* codec    = nullptr;
        video_stream_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (video_stream_idx_ < 0 || !codec) {
            SafeCloseContext();
            LOG_WARN("{}FindStream {} stream or codec error.[{}]", kTag, filename_, GetAvErr(ret));
            return util::ErrorEnum::DemuxFindVideoStreamFail;
        }

        std::string codec_name = codec->name;
        if (codec_name == "h264" || codec_name == "h264_bm") {
            enc_type_ = VideoCodecType::kH264;
        } else if (codec_name == "hevc" || codec_name == "hevc_bm") {
            enc_type_ = VideoCodecType::kH265;
        } else if (codec_name == "mjpeg" || codec_name == "mjpg") {
            enc_type_ = VideoCodecType::kMjpeg;  // USB camera V4L2 MJPEG (consistent with old eb6daaa6)
        } else {
            SafeCloseContext();
            LOG_WARN("{}FindStream {} codec is not support. [{}]", kTag, filename_, codec_name);
            return util::ErrorEnum::VideoFormatNotSupport;
        }

        AVStream* stream            = fmt_ctx_->streams[video_stream_idx_];
        AVCodecParameters* codecpar = stream->codecpar;
        AVRational avgFpsRat        = stream->avg_frame_rate;
        AVRational codecFpsRat      = av_guess_frame_rate(fmt_ctx_, stream, nullptr);
        AVRational streamFpsRat     = stream->r_frame_rate;

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
                LOG_WARN("{}FindStream {} init bsfc Failed", kTag, filename_);
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
            kTag, filename_, video_stream_idx_, codec->name, codecpar->width, codecpar->height, fps_,
            avgFpsRat.num, avgFpsRat.den, codecFpsRat.num, codecFpsRat.den, streamFpsRat.num,
            streamFpsRat.den);

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
                cond_.wait_for(lockControl, expectTime - diffTime);
            }
        }
    }

    int VideoDemuxer::AvReadFrame(AVPacket* packet) {
        int ret = 0;
        do {
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

        auto now     = std::chrono::steady_clock::now();
        auto ptsDiff = packet->pts - start_pts_;

        // 2025-07-16 Added ptsDiff > 0 to prevent positive result
        // on overflow of (packet->pts - m_startPts). A Hikvision camera may output 0x8000000000000000
        // timestamp
        if (ptsDiff > 0) {
            auto ptsCount = packet_index_ - fps_calc_frame_;
            float fpsReceive =
                static_cast<float>(ptsCount) * 1000000.0f /
                static_cast<float>(
                    std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_point_).count());
            int64_t avrageDur = ptsDiff / ptsCount;
            auto timeBase     = fmt_ctx_->streams[video_stream_idx_]->time_base;
            float fpsNewf = static_cast<float>(timeBase.den) / static_cast<float>(avrageDur * timeBase.num);
            auto c_fps    = fps_.load(std::memory_order_relaxed);
            // Use real-time frame rate if timestamp is incorrect
            if (start_pts_ > packet->pts) {
                fpsNewf = fpsReceive;
            }
            if (fpsNewf <= 200 && std::fabs(fpsNewf - c_fps) > 1.0f) {
                LOG_INFO(
                    "{}{} {}x{} fps_: {} -> {}({}) avrageDur:{} num:{} den:{} fpsReceive:[{}] "
                    "ptsCount:{}, duration:{}, packet.pts:{}, startPts:{}",
                    kTag, filename_, fmt_ctx_->streams[video_stream_idx_]->codecpar->width,
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

        auto ptsDiff = packet->pts - pts_;

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
        int64_t frame_number = frame_index_ + 1;
        if (ptsDiff > (duration_active_ + (duration_active_ >> 1))) {
            frame_number = frame_index_ + ptsDiff / duration_active_;
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
        if (!ready_) {
            return ReadFrameStatus::StreamNotOpen;
        }
        if (end_) {
            return ReadFrameStatus::StreamEnd;
        }
        LocalFileFpsCtrl();

        AVPacket packet;
        int ret = AvReadFrame(&packet);
        //    m_packetIndex++;
        if (0 != ret) {
            LOG_INFO("{}{} get frame error: {} Maybe It's Finished", kTag, filename_,
                     GetAvErr(ret));  // Reached end_ of playback
            end_            = true;
            pkt->stream_idx = stream_opened_cnt_;
            av_packet_unref(&packet);
            return ReadFrameStatus::StreamEnd;
        }

        if (packet.size <= 4) {
            LOG_INFO("{}{} empty frame.", kTag, filename_);
            av_packet_unref(&packet);
            return ReadFrameStatus::EmptyFrame;
        }

        if (!key_frame_detected_) {
            key_frame_detected_ = packet.flags & AV_PKT_FLAG_KEY;
            if (!key_frame_detected_) {
                av_packet_unref(&packet);
                return ReadFrameStatus::NotGetIFrame;
            }
            LOG_INFO("{}{} detected key frame.", kTag, filename_);
        }

        // rtsp frame rate may be incorrect, calculate via pts_
        CalcFps(&packet);
        // Some streams have incorrect frame sequence
        CalcFrameIndex(&packet);
        // Get stream fetching timestamp
        auto timestamp = util::GetMilliseconds();
        auto timePoint = std::chrono::steady_clock::now();

        // Save pts and keyframe flags before sending packet to bitstream filter (which resets/clears it)
        const int64_t saved_pts     = packet.pts;
        const bool saved_is_i_frame = static_cast<bool>(packet.flags & AV_PKT_FLAG_KEY);

        // Video files convert to 0x00000001, rtsp does not
        if (strategy_ && strategy_->NeedsBsf(enc_type_)) {
            if (bsf_ctx_) {
                ret = av_bsf_send_packet(bsf_ctx_, &packet);
                if (ret < 0) {
                    LOG_WARN("{}{} av_bsf_send_packet failed: {}", kTag, filename_, GetAvErr(ret));
                    av_packet_unref(&packet);
                    return ReadFrameStatus::EmptyFrame;
                }

                AVPacket filtered_pkt;
                av_init_packet(&filtered_pkt);
                filtered_pkt.data = nullptr;
                filtered_pkt.size = 0;

                ret = av_bsf_receive_packet(bsf_ctx_, &filtered_pkt);
                if (ret < 0) {
                    LOG_WARN("{}{} av_bsf_receive_packet failed: {}", kTag, filename_, GetAvErr(ret));
                    av_packet_unref(&packet);
                    return ReadFrameStatus::EmptyFrame;
                }

                // Diagnostic: log BSF result for first 3 packets per stream
                if (abs_packet_index_ < 3 && filtered_pkt.size >= 5) {
                    LOG_INFO(
                        "{}{} BSF pkt#{} inSize={} outSize={} bytes:{:02X} {:02X} {:02X} "
                        "{:02X} {:02X}",
                        kTag, filename_, abs_packet_index_, packet.size, filtered_pkt.size,
                        filtered_pkt.data[0], filtered_pkt.data[1], filtered_pkt.data[2],
                        filtered_pkt.data[3], filtered_pkt.data[4]);
                }

                pkt->data = std::vector<uint8_t>(filtered_pkt.data, filtered_pkt.data + filtered_pkt.size);
                av_packet_unref(&filtered_pkt);
            } else {
                LOG_WARN("{}{} bsf_ctx_ is NULL, skipping BSF", kTag, filename_);
                pkt->data = std::vector<uint8_t>(packet.data, packet.data + packet.size);
            }
        } else {
            pkt->data = std::vector<uint8_t>(packet.data, packet.data + packet.size);
        }

        packet_index_++;
        abs_packet_index_++;
        pkt->pts       = saved_pts;
        pkt->timestamp = timestamp;
        pkt->timestamp_epoch =
            std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count();
        pkt->time_point = timePoint;
        pkt->stream_idx = stream_opened_cnt_;
        pkt->index      = packet_index_;
        pkt->abs_idx    = abs_packet_index_;
        pkt->is_i_frame = saved_is_i_frame;
        pkt->codec_type = enc_type_;
        pkt->width      = static_cast<size_t>(width_.load());
        pkt->height     = static_cast<size_t>(height_.load());
        pkt->fps        = fps_;

        // NOTE: Intentionally ignoring dynamic resolution changes here to prevent pipeline crashes.

        av_packet_unref(&packet);
        return ReadFrameStatus::Success;
    }

    void VideoDemuxer::SafeCloseContext() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (fmt_ctx_) {
            avformat_close_input(&fmt_ctx_);
            fmt_ctx_ = nullptr;
            LOG_INFO("{}Stream {} fmt_ctx_ Closed.", kTag, filename_);
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
            LOG_WARN("{}{} BSF '{}' not found", kTag, filename_, bsf_name);
            return false;
        }

        int ret = av_bsf_alloc(filter, &bsf_ctx_);
        if (ret < 0) {
            LOG_WARN("{}{} av_bsf_alloc failed: {}", kTag, filename_, GetAvErr(ret));
            return false;
        }

        // Copy codec parameters to BSF context input
        ret = avcodec_parameters_copy(bsf_ctx_->par_in, fmt_ctx_->streams[video_stream_idx_]->codecpar);
        if (ret < 0) {
            LOG_WARN("{}{} avcodec_parameters_copy failed: {}", kTag, filename_, GetAvErr(ret));
            av_bsf_free(&bsf_ctx_);
            return false;
        }

        ret = av_bsf_init(bsf_ctx_);
        if (ret < 0) {
            LOG_WARN("{}{} av_bsf_init failed: {}", kTag, filename_, GetAvErr(ret));
            av_bsf_free(&bsf_ctx_);
            return false;
        }

        LOG_INFO("{}{} BSF '{}' initialized successfully", kTag, filename_, bsf_name);
        return true;
    }

}  // namespace media
}  // namespace cosmo
