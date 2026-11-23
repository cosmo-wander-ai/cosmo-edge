// VideoDemuxerStream.cc — Stream handling for VideoDemuxer.
// Split from VideoDemuxer.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

#include "media/FrameRateReporting.h"
#include "media/H264AnnexBColorParser.h"
#include "media/NativeDemuxAdmission.h"
#include "media/NativeRtspParameterSets.h"
#include "media/VideoDemuxColorEvidence.h"
#include "media/VideoDemuxer.h"
#include "media/VideoPacketColorWitness.h"
#include "util/Log.h"
#include "util/RtspUrlUtil.h"
#include "util/TimeUtil.h"

static constexpr const char* kTag = "[DEMUX] ";

namespace cosmo {
namespace media {

    static NativeVideoColorSpace ContainerColorSpace(AVColorSpace value) {
        switch (value) {
            case AVCOL_SPC_BT709:
                return NativeVideoColorSpace::Bt709;
            case AVCOL_SPC_BT470BG:
            case AVCOL_SPC_SMPTE170M:
                return NativeVideoColorSpace::Bt601;
            case AVCOL_SPC_UNSPECIFIED:
                return NativeVideoColorSpace::Unspecified;
            default:
                return NativeVideoColorSpace::Unsupported;
        }
    }

    static NativeVideoColorRange ContainerColorRange(AVColorRange value) {
        if (value == AVCOL_RANGE_MPEG)
            return NativeVideoColorRange::Limited;
        if (value == AVCOL_RANGE_JPEG)
            return NativeVideoColorRange::Full;
        return NativeVideoColorRange::Unspecified;
    }

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
        // The witnessed native AX route cannot permit libavformat to open a
        // software video decoder during stream discovery. Container metadata
        // must already be complete; missing fields fail closed. Other routes
        // retain the existing probe and codec discovery behavior.
        bool strict_native = false;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        strict_native = native_color_witness_enabled_;
#endif
        int ret = 0;
        if ((!strict_native && (ret = avformat_find_stream_info(fmt_ctx_, nullptr)) < 0) || StopRequested()) {
            CloseStream();
            LOG_WARN("{}FindStream {} failed.[{}]", kTag, util::RedactRtspUrl(filename_), GetAvErr(ret));
            return util::ErrorEnum::DemuxFindStreamFail;
        }

        AVCodec* codec = nullptr;
        video_stream_idx_ =
            av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, strict_native ? nullptr : &codec, 0);
        if (video_stream_idx_ < 0 || (!strict_native && !codec)) {
            const int stream_error = video_stream_idx_ < 0 ? video_stream_idx_ : AVERROR_DECODER_NOT_FOUND;
            CloseStream();
            LOG_WARN("{}FindStream {} stream or codec error.[{}]", kTag, util::RedactRtspUrl(filename_),
                     GetAvErr(stream_error));
            return util::ErrorEnum::DemuxFindVideoStreamFail;
        }
        // RTSP SDP can provide Annex-B SPS/PPS but no codecpar dimensions
        // until stream probing. Recover only geometry from those compressed
        // parameter sets; native AU/frame witness remains independently required.
        if (strict_native && filename_.compare(0, 7, "rtsp://") == 0 &&
            static_cast<unsigned>(video_stream_idx_) < fmt_ctx_->nb_streams &&
            fmt_ctx_->streams[video_stream_idx_]) {
            AVCodecParameters* params = fmt_ctx_->streams[video_stream_idx_]->codecpar;
            if (params && params->codec_id == AV_CODEC_ID_H264 &&
                (params->width <= 0 || params->height <= 0)) {
                uint32_t width = 0, height = 0;
                if (!H264AnnexBColorParser::ParseParameterSetGeometry(
                        params->extradata,
                        params->extradata_size > 0 ? static_cast<size_t>(params->extradata_size) : 0, width,
                        height) ||
                    (params->width > 0 && static_cast<uint32_t>(params->width) != width) ||
                    (params->height > 0 && static_cast<uint32_t>(params->height) != height)) {
                    CloseStream();
                    return util::ErrorEnum::DemuxFindStreamFail;
                }
                params->width  = static_cast<int>(width);
                params->height = static_cast<int>(height);
            }
        }
        if (strict_native &&
            !NativeDemuxMetadataReady(fmt_ctx_, video_stream_idx_,
                                      strategy_ && strategy_->NeedsBsf(VideoCodecType::kH264))) {
            CloseStream();
            LOG_WARN("{}{} native stream metadata incomplete", kTag, util::RedactRtspUrl(filename_));
            return util::ErrorEnum::DemuxFindStreamFail;
        }
        const AVCodecParameters* selected = fmt_ctx_->streams[video_stream_idx_]->codecpar;
        if (!selected) {
            CloseStream();
            return util::ErrorEnum::DemuxFindStreamFail;
        }
        std::string codec_name = strict_native ? avcodec_get_name(selected->codec_id) : codec->name;
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

        // Cache extradata so new RTMP viewers can get SPS/PPS
        // without waiting for in-band parameters in the stream.
        {
            // Preview startup can read headers while this worker reconnects.
            // Publish/copy snapshots rather than returning a mutable reference.
            std::lock_guard<std::mutex> lock(metadata_mtx_);
            enc_type_ = next_codec;
            if (codecpar->extradata && codecpar->extradata_size > 0) {
                extradata_.assign(codecpar->extradata, codecpar->extradata + codecpar->extradata_size);
            } else {
                extradata_.clear();
            }
        }

        AVRational avgFpsRat    = stream->avg_frame_rate;
        AVRational codecFpsRat  = av_guess_frame_rate(fmt_ctx_, stream, nullptr);
        AVRational streamFpsRat = stream->r_frame_rate;

        width_.store(codecpar->width, std::memory_order_relaxed);
        height_.store(codecpar->height, std::memory_order_relaxed);

        if (strict_native && !NativeDemuxForcedFpsReady(video_force_fps_)) {
            CloseStream();
            return util::ErrorEnum::DemuxFindStreamFail;
        }
        if (strict_native && strategy_ && strategy_->NeedsFpsControl() &&
            !NativeDemuxFpsMetadataReady(stream)) {
            CloseStream();
            return util::ErrorEnum::DemuxFindStreamFail;
        }
        fps_.store(SelectBestFps(avgFpsRat, codecFpsRat, streamFpsRat), std::memory_order_relaxed);

        auto c_fps                  = fps_.load(std::memory_order_relaxed);
        const bool has_fps_metadata = (avgFpsRat.num != 0 && avgFpsRat.den != 0) ||
                                      (codecFpsRat.num != 0 && codecFpsRat.den != 0) ||
                                      (streamFpsRat.num != 0 && streamFpsRat.den != 0);
        // On native live streams, discovery intentionally avoids opening a
        // software decoder. Do not present its internal 25 fps fallback as
        // measured stream metadata when discovery found no usable rate.
        reported_fps_unknown_.store(
            NativeLiveFrameRateUnknown(strict_native, strategy_ && strategy_->IsLive(), has_fps_metadata,
                                       c_fps),
            std::memory_order_relaxed);

        if ((strict_native && (!std::isfinite(c_fps) || c_fps <= 0 || c_fps > 1000)) ||
            (!strict_native && (c_fps == 0 || c_fps > 1000))) {
            if (strict_native && strategy_ && strategy_->NeedsFpsControl()) {
                CloseStream();
                return util::ErrorEnum::DemuxFindStreamFail;
            }
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
                if (strict_native && (!std::isfinite(video_force_fps_) || video_force_fps_ > 1000)) {
                    CloseStream();
                    return util::ErrorEnum::DemuxFindStreamFail;
                }
                fps_.store(video_force_fps_, std::memory_order_relaxed);
            }
        }

        LOG_INFO(
            "{}{} videoStreamIdx:{} codec:{} {}x{} fps_:{} avg[num:{} den:{}], codec[num:{} den:{}], "
            "stream[num:{}, den:{}]",
            kTag, util::RedactRtspUrl(filename_), video_stream_idx_, codec_name, codecpar->width,
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
        // Used to control frame rate for local files. Use one rate snapshot
        // for the check and calculation, even if another thread updates it.
        const float rate = fps_.load(std::memory_order_relaxed);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_color_witness_enabled_ && strategy_ && strategy_->NeedsFpsControl() &&
            !native_presentation::NativeFpsControlSafe(packet_index_, rate)) {
            native_fps_control_fault_ = true;
            return;
        }
#endif
        if (strategy_ && strategy_->NeedsFpsControl() && rate > 0.0f) {
            std::chrono::milliseconds expectTime(
                static_cast<int64_t>(static_cast<float>(packet_index_ * 1000) / rate));
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
        if (!pkt)
            return ReadFrameStatus::EmptyFrame;
        // Callers may reuse a packet object. Never retain a prior AU's proof or
        // derived clock on a later failure/discontinuity.
        pkt->presentation_time_valid   = false;
        pkt->presentation_timestamp_ms = 0;
        pkt->presentation_steady_ms    = 0;
        pkt->color_witness.reset();
        if (StopRequested() || !ready_ || !fmt_ctx_) {
            return ReadFrameStatus::StreamNotOpen;
        }
        if (end_) {
            return ReadFrameStatus::StreamEnd;
        }
        LocalFileFpsCtrl();
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_color_witness_enabled_ && native_fps_control_fault_) {
            native_presentation_fault_ = true;
            end_                       = true;
            return ReadFrameStatus::StreamEnd;
        }
#endif
        if (StopRequested())
            return ReadFrameStatus::StreamNotOpen;

        AVPacket packet{};
        int ret = AvReadFrame(&packet);
        //    m_packetIndex++;
        if (0 != ret) {
            LOG_INFO("{}{} get frame error: {} Maybe It's Finished", kTag, util::RedactRtspUrl(filename_),
                     GetAvErr(ret));  // Reached end_ of playback
            end_            = true;
            pkt->stream_idx = stream_opened_cnt_;
            av_packet_unref(&packet);
            return ReadFrameStatus::StreamEnd;
        }

        if (packet.size <= 4) {
            LOG_INFO("{}{} empty frame.", kTag, util::RedactRtspUrl(filename_));
            av_packet_unref(&packet);
            return ReadFrameStatus::EmptyFrame;
        }

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        // Drop initial RTSP packets without PTS before keyframe admission;
        // do not mint a witness or infer a presentation timestamp.
        if (native_color_witness_enabled_ && filename_.compare(0, 7, "rtsp://") == 0 &&
            !native_presentation_ready_ && packet.pts == AV_NOPTS_VALUE) {
            av_packet_unref(&packet);
            return ReadFrameStatus::EmptyFrame;
        }
#endif
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        const bool native_rtsp_h264_key_au =
            native_color_witness_enabled_ && filename_.compare(0, 7, "rtsp://") == 0 &&
            enc_type_ == VideoCodecType::kH264 && (packet.flags & AV_PKT_FLAG_KEY);
        const bool first_native_rtsp_h264_au = native_rtsp_h264_key_au && !key_frame_detected_;
#endif
        if (!key_frame_detected_) {
            key_frame_detected_ = packet.flags & AV_PKT_FLAG_KEY;
            if (!key_frame_detected_) {
                av_packet_unref(&packet);
                return ReadFrameStatus::NotGetIFrame;
            }
            LOG_INFO("{}{} detected key frame.", kTag, util::RedactRtspUrl(filename_));
        }

        // The legacy estimators subtract packet-order PTS in signed arithmetic
        // and assume decode order is presentation order. On strict native
        // streams use the inspected stream FPS and packet_index_ instead; the
        // PTS mapper below validates arbitrary reorder and extreme values.
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (!native_color_witness_enabled_) {
            CalcFps(&packet);
            CalcFrameIndex(&packet);
        }
#else
        CalcFps(&packet);
        CalcFrameIndex(&packet);
#endif
        // Get stream fetching timestamp
        auto timestamp = util::GetMilliseconds();
        auto timePoint = std::chrono::steady_clock::now();

        // Save pts and keyframe flags before sending packet to bitstream filter (which resets/clears it)
        const int64_t saved_pts            = packet.pts;
        const bool saved_is_i_frame        = static_cast<bool>(packet.flags & AV_PKT_FLAG_KEY);
        bool color_pts_correlated          = saved_pts != AV_NOPTS_VALUE;
        bool color_parameter_change        = HasColorRelevantParameterChange(packet);
        const auto* codecpar               = fmt_ctx_->streams[video_stream_idx_]->codecpar;
        const bool container_color_present = codecpar && (codecpar->color_space != AVCOL_SPC_UNSPECIFIED ||
                                                          codecpar->color_range != AVCOL_RANGE_UNSPECIFIED);
        const auto container_space =
            codecpar ? ContainerColorSpace(codecpar->color_space) : NativeVideoColorSpace::Unspecified;
        const auto container_range =
            codecpar ? ContainerColorRange(codecpar->color_range) : NativeVideoColorRange::Unspecified;

        // Video files convert to 0x00000001, rtsp does not
        if (strategy_ && strategy_->NeedsBsf(enc_type_)) {
            if (bsf_ctx_) {
                ret = av_bsf_send_packet(bsf_ctx_, &packet);
                if (ret < 0) {
                    LOG_WARN("{}{} av_bsf_send_packet failed: {}", kTag, util::RedactRtspUrl(filename_),
                             GetAvErr(ret));
                    av_packet_unref(&packet);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
                    if (native_color_witness_enabled_) {
                        native_presentation_fault_ = true;
                        end_                       = true;
                        return ReadFrameStatus::StreamEnd;
                    }
#endif
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
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
                    if (native_color_witness_enabled_) {
                        native_presentation_fault_ = true;
                        end_                       = true;
                        return ReadFrameStatus::StreamEnd;
                    }
#endif
                    return ReadFrameStatus::EmptyFrame;
                }

                color_pts_correlated = color_pts_correlated && filtered_pkt.pts == saved_pts;
                color_parameter_change =
                    color_parameter_change || HasColorRelevantParameterChange(filtered_pkt);

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
                av_packet_unref(&filtered_pkt);
            } else {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
                if (native_color_witness_enabled_) {
                    native_presentation_fault_ = true;
                    end_                       = true;
                    av_packet_unref(&packet);
                    return ReadFrameStatus::StreamEnd;
                }
#endif
                LOG_WARN("{}{} bsf_ctx_ is NULL, skipping BSF", kTag, util::RedactRtspUrl(filename_));
                pkt->data = std::vector<uint8_t>(packet.data, packet.data + packet.size);
            }
        } else {
            pkt->data = std::vector<uint8_t>(packet.data, packet.data + packet.size);
        }

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        // Include known SDP parameter sets on later keyframes too: a decoder
        // recreated after a packet gap must be able to start at any keyframe.
        const bool can_recover_from_sdp = codecpar && codecpar->extradata && codecpar->extradata_size > 0;
        const bool prepared_native_parameters =
            !native_rtsp_h264_key_au || (!first_native_rtsp_h264_au && !can_recover_from_sdp) ||
            native_rtsp::AddMissingParameterSets(
                codecpar ? codecpar->extradata : nullptr,
                codecpar && codecpar->extradata_size > 0 ? static_cast<size_t>(codecpar->extradata_size) : 0,
                pkt->data);
        if (first_native_rtsp_h264_au && !prepared_native_parameters) {
            LOG_WARN("{}{} native H264 parameter sets unavailable", kTag, util::RedactRtspUrl(filename_));
            native_presentation_fault_ = true;
            end_                       = true;
            av_packet_unref(&packet);
            return ReadFrameStatus::StreamEnd;
        }
#endif
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_color_witness_enabled_ && (packet_index_ == std::numeric_limits<int64_t>::max() ||
                                              abs_packet_index_ == std::numeric_limits<int64_t>::max())) {
            native_presentation_fault_ = true;
            av_packet_unref(&packet);
            return ReadFrameStatus::StreamEnd;
        }
#endif
        packet_index_++;
        abs_packet_index_++;
        pkt->pts                         = saved_pts;
        pkt->color_packet_pts_correlated = color_pts_correlated;
        pkt->color_parameter_change      = color_parameter_change;
        pkt->color_container_present     = container_color_present;
        pkt->container_color_space       = container_space;
        pkt->container_color_range       = container_range;
        pkt->timestamp                   = timestamp;
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

        // Arrival clocks remain untouched. For the opted-in native path, the
        // presentation timeline is mapped from this AU's actual PTS using a
        // single wall/steady anchor per stream generation. The ±30 s bound is
        // conservative: unbounded drift/discontinuity is rejected, not clamped.
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_color_witness_enabled_ && !native_presentation_fault_) {
            const auto arrival_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count();
            const auto tb = fmt_ctx_->streams[video_stream_idx_]->time_base;
            if (!native_presentation_ready_) {
                native_presentation_ready_ = native_presentation_clock_.Initialize(
                    saved_pts, tb.num, tb.den, timestamp, arrival_ms, 30000);
                native_presentation_timebase_num_ = tb.num;
                native_presentation_timebase_den_ = tb.den;
            }
            const bool same_timebase =
                tb.num == native_presentation_timebase_num_ && tb.den == native_presentation_timebase_den_;
            const auto mapped = native_presentation_ready_ && same_timebase
                                    ? native_presentation_clock_.Convert(saved_pts, arrival_ms)
                                    : std::nullopt;
            if (mapped) {
                pkt->presentation_time_valid   = true;
                pkt->presentation_timestamp_ms = mapped->wall_ms;
                pkt->presentation_steady_ms    = mapped->steady_ms;
            } else {
                native_presentation_fault_ = true;
            }
        }
        // Both arrival and derived presentation values are included in this
        // demux-minted packet witness and checked against the retained AU.
        pkt->color_witness = native_color_witness_enabled_ && pkt->presentation_time_valid
                                 ? VideoPacketColorWitness::Create(*pkt)
                                 : nullptr;
#else
        pkt->color_witness = native_color_witness_enabled_ ? VideoPacketColorWitness::Create(*pkt) : nullptr;
#endif

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

        ret = av_bsf_init(bsf_ctx_);
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
