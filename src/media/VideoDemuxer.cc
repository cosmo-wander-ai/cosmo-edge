// VideoDemuxer — Video Demuxer implementation.

#include "media/VideoDemuxer.h"

#include <thread>

#include "media/FileDemuxStrategy.h"
#include "media/NetworkDemuxStrategy.h"
#include "media/RtspDemuxStrategy.h"
#include "media/UsbDemuxStrategy.h"
#include "util/Log.h"
#include "util/ProcessShutdown.h"
#include "util/RtspUrlUtil.h"
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

    void VideoDemuxer::RequestStop() {
        stop_requested_.store(true);
        cond_.notify_all();
    }

    void VideoDemuxer::ResetCancellation() {
        stop_requested_.store(false);
    }

    bool VideoDemuxer::StopRequested() const {
        return stop_requested_.load() || util::ProcessShutdown::Requested() ||
               (io_running_ && !io_running_->load()) ||
               (io_deadline_ != std::chrono::steady_clock::time_point::max() &&
                std::chrono::steady_clock::now() >= io_deadline_);
    }

    void VideoDemuxer::SetIoDeadline(std::chrono::steady_clock::time_point deadline,
                                     const std::atomic<bool>* running) {
        io_deadline_ = deadline;
        io_running_  = running;
    }

    int VideoDemuxer::InterruptIo(void* opaque) {
        return static_cast<VideoDemuxer*>(opaque)->StopRequested() ? 1 : 0;
    }

    // Stop video stream on destruction
    VideoDemuxer::~VideoDemuxer() {
        LOG_INFO("{}Read {} closed.", kTag, util::RedactRtspUrl(filename_));

        CloseStream();
        LOG_INFO("{}Read {} Delete.", kTag, util::RedactRtspUrl(filename_));
    }

    void VideoDemuxer::SetFile(const std::string& videoFile) {
        if (videoFile != filename_) {
            LOG_INFO("{}Change File From {} To {}", kTag, util::RedactRtspUrl(filename_),
                     util::RedactRtspUrl(videoFile));
            filename_ = videoFile;
        }
        LOG_INFO("{}Ready To Read {}", kTag, util::RedactRtspUrl(filename_));
        return;
    }

    void VideoDemuxer::SetForceFps(float new_fps) {
        if (video_force_fps_ != new_fps) {
            LOG_INFO("{}:{} VideoFps Change From {} To {}", kTag, util::RedactRtspUrl(filename_),
                     video_force_fps_, new_fps);
            video_force_fps_ = new_fps;
        }
    }

    void VideoDemuxer::CloseStream(bool bRepeat) {
        // Looping playback requires VoD file
        if (bRepeat && !StopRequested() && opened_ && ready_ && filename_ == opened_filename_ && strategy_ &&
            strategy_->SupportsRepeat()) {
            return;
        }

        opened_ = false;
        ready_  = false;

        if (bsf_ctx_) {
            av_bsf_free(&bsf_ctx_);
            bsf_ctx_ = nullptr;
            LOG_INFO("{}Stream {} bsf_ctx_ Closed.", kTag, util::RedactRtspUrl(filename_));
        }

        SafeCloseContext();
        opened_filename_.clear();
        {
            std::lock_guard<std::mutex> lock(metadata_mtx_);
            extradata_.clear();
        }
        width_.store(0, std::memory_order_relaxed);
        height_.store(0, std::memory_order_relaxed);
        fps_.store(0, std::memory_order_relaxed);
        LOG_INFO("{}Stream {} Closed.", kTag, util::RedactRtspUrl(filename_));
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
        if (file.compare(0, 7, "rtmp://") == 0) {
            return std::make_unique<NetworkDemuxStrategy>(pullTimeoutSec);
        }
        // Local file / HTTP file
        return std::make_unique<FileDemuxStrategy>();
    }

    // Open stream: Verify online status if this function is available
    // pullTimeoutSec default 5 seconds, delayMs default 200ms
    util::ErrorEnum VideoDemuxer::OpenStream(bool bRepeat, int pullTimeoutSec, int delayMs) {
        if (StopRequested()) {
            CloseStream();
            return util::ErrorEnum::DemuxOpenStreamFail;
        }
        // Looping playback requires VoD file
        if (bRepeat && opened_ && ready_ && fmt_ctx_ && filename_ == opened_filename_ && strategy_ &&
            strategy_->SupportsRepeat()) {
            ResetStreamState();
            opened_ = true;
            ready_  = true;

            // Seek back to the beginning of the file
            int seekRet = av_seek_frame(fmt_ctx_, video_stream_idx_, 0, AVSEEK_FLAG_BACKWARD);
            if (seekRet < 0) {
                LOG_WARN("{}Seek to beginning failed for {}: [{}]", kTag, util::RedactRtspUrl(filename_),
                         GetAvErr(seekRet));
                seekRet = avformat_seek_file(fmt_ctx_, video_stream_idx_, INT64_MIN, 0, INT64_MAX, 0);
                if (seekRet < 0) {
                    LOG_WARN("{}avformat_seek_file also failed for {}: [{}]", kTag,
                             util::RedactRtspUrl(filename_), GetAvErr(seekRet));
                    CloseStream();
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

        // A probed context has released its FFmpeg stream-probe state and
        // cannot be reused as a fresh input, even if the URL is unchanged.
        CloseStream();
        ResetStreamState();

        // Release old strategy and its resources before creating a new one
        strategy_.reset();
        strategy_ = CreateStrategy(filename_, pullTimeoutSec, delayMs);
        is_live_.store(strategy_->IsLive(), std::memory_order_relaxed);
        // Install cancellation before avformat_open_input, not just before
        // reading packets: connect/handshake and probing can also block.
        fmt_ctx_ = avformat_alloc_context();
        if (!fmt_ctx_)
            return util::ErrorEnum::DemuxOpenStreamFail;
        fmt_ctx_->interrupt_callback = {&VideoDemuxer::InterruptIo, this};
        auto ret                     = strategy_->OpenInput(fmt_ctx_, filename_);
        if (ret == util::ErrorEnum::Success && fmt_ctx_ && !StopRequested()) {
            opened_          = true;
            opened_filename_ = filename_;
        } else {
            // Ensure fmt_ctx_ is freed on failure — some OpenInput implementations
            // may partially allocate the context before failing.
            CloseStream();
            if (ret == util::ErrorEnum::Success)
                ret = util::ErrorEnum::DemuxOpenStreamFail;
        }
        return ret;
    }

    // Stream handling — moved to VideoDemuxerStream.cc

}  // namespace media
}  // namespace cosmo
