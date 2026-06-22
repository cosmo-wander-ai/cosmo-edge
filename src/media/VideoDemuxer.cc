// VideoDemuxer — Video Demuxer implementation.

#include "media/VideoDemuxer.h"

#include <thread>

#include "media/FileDemuxStrategy.h"
#include "media/RtspDemuxStrategy.h"
#include "media/UsbDemuxStrategy.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

static constexpr const char* kTag = "[DEMUX] ";

namespace cosmo {
namespace media {
    std::string GetAvErr(int errorNo) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errorNo, buf, AV_ERROR_MAX_STRING_SIZE);
        return buf;
    }

    VideoDemuxer::VideoDemuxer() : fmt_ctx_(nullptr), video_stream_idx_(0), key_frame_detected_(false) {
        opened_ = false;
    }

    // Stop video stream on destruction
    VideoDemuxer::~VideoDemuxer() {
        LOG_INFO("{}Read {} closed.", kTag, filename_);

        CloseStream();
        LOG_INFO("{}Read {} Delete.", kTag, filename_);
    }

    void VideoDemuxer::SetFile(const std::string& videoFile) {
        if (videoFile != filename_) {
            LOG_INFO("{}Change File From {} To {}", kTag, filename_, videoFile);
            filename_ = videoFile;
        }
        LOG_INFO("{}Ready To Read {}", kTag, filename_);
        return;
    }

    void VideoDemuxer::SetForceFps(float new_fps) {
        if (video_force_fps_ != new_fps) {
            LOG_INFO("{}:{} VideoFps Change From {} To {}", kTag, filename_, video_force_fps_, new_fps);
            video_force_fps_ = new_fps;
        }
    }

    void VideoDemuxer::CloseStream(bool bRepeat) {
        // Looping playback requires VoD file
        if (bRepeat && strategy_ && strategy_->SupportsRepeat()) {
            return;
        }

        opened_ = false;
        ready_  = false;

        if (bsf_ctx_) {
            av_bsf_free(&bsf_ctx_);
            bsf_ctx_ = nullptr;
            LOG_INFO("{}Stream {} bsf_ctx_ Closed.", kTag, filename_);
        }

        SafeCloseContext();
        LOG_INFO("{}Stream {} Closed.", kTag, filename_);
        return;
    }

    void VideoDemuxer::ResetStreamState() {
        opened_ = false;
        ready_  = false;
        end_    = false;
        pts_    = 0;
        stream_opened_cnt_ += 1;
        frame_index_      = 0;
        packet_index_     = 0;
        start_pts_        = 0;
        open_time_point_  = std::chrono::steady_clock::now();
        start_time_point_ = open_time_point_;

        duration_active_       = -1;
        duration_real_time_    = 100000000;
        duration_active_count_ = 0;
        time_diff_count_       = 0;
        abs_packet_index_      = 0;
        key_frame_detected_    = false;
        fps_calc_frame_        = 0;
    }

    std::unique_ptr<IDemuxStrategy> VideoDemuxer::CreateStrategy(const std::string& file, int pullTimeoutSec,
                                                                 int delayMs) {
        if (file.compare(0, 6, "usb://") == 0) {
            return std::make_unique<UsbDemuxStrategy>();
        }
        if (file.compare(0, 7, "rtsp://") == 0) {
            return std::make_unique<RtspDemuxStrategy>(pullTimeoutSec, delayMs);
        }
        // Local file / HTTP file
        return std::make_unique<FileDemuxStrategy>();
    }

    // Open stream: Verify online status if this function is available
    // pullTimeoutSec default 5 seconds, delayMs default 200ms
    util::ErrorEnum VideoDemuxer::OpenStream(bool bRepeat, int pullTimeoutSec, int delayMs) {
        // Looping playback requires VoD file
        if (bRepeat && strategy_ && strategy_->SupportsRepeat()) {
            if (!opened_)
                return util::ErrorEnum::DemuxOpenStreamFail;

            ResetStreamState();
            opened_ = true;
            ready_  = true;

            // Seek back to the beginning of the file
            int seekRet = av_seek_frame(fmt_ctx_, video_stream_idx_, 0, AVSEEK_FLAG_BACKWARD);
            if (seekRet < 0) {
                LOG_WARN("{}Seek to beginning failed for {}: [{}]", kTag, filename_, GetAvErr(seekRet));
                seekRet = avformat_seek_file(fmt_ctx_, video_stream_idx_, INT64_MIN, 0, INT64_MAX, 0);
                if (seekRet < 0) {
                    LOG_WARN("{}avformat_seek_file also failed for {}: [{}]", kTag, filename_,
                             GetAvErr(seekRet));
                    opened_ = false;
                    ready_  = false;
                    return util::ErrorEnum::DemuxOpenStreamFail;
                }
            }

            // Flush BSF internal state for the new loop iteration.
            // The new AVBSFContext API uses a copy of codecpar, so flushing is safe
            // (no in-place extradata mutation like the deprecated API).
            if (bsf_ctx_) {
                av_bsf_flush(bsf_ctx_);
            }

            return util::ErrorEnum::Success;
        }

        ResetStreamState();

        // Release old strategy and its resources before creating a new one
        strategy_.reset();
        strategy_ = CreateStrategy(filename_, pullTimeoutSec, delayMs);
        auto ret  = strategy_->OpenInput(fmt_ctx_, filename_);
        if (ret == util::ErrorEnum::Success && fmt_ctx_) {
            opened_ = true;
        } else {
            // Ensure fmt_ctx_ is freed on failure — some OpenInput implementations
            // may partially allocate the context before failing.
            SafeCloseContext();
        }
        return ret;
    }

    // Stream handling — moved to VideoDemuxerStream.cc

}  // namespace media
}  // namespace cosmo
