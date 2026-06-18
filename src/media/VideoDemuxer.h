#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "media/IDemuxStrategy.h"
#include "media/VideoPacket.h"
#include "util/ErrorCode.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#ifdef __cplusplus
}
#endif

namespace cosmo {
namespace media {

    // H.264 and H.265 NAL unit type definitions
    constexpr int kH264NalSps = 7;
    constexpr int kH265NalSps = 33;

    enum class ReadFrameStatus {
        Success = 0,       // Success
        StreamNotOpen,     // Stream not open
        StreamEnd,         // Stream end reached
        EmptyFrame,        // Empty frame, need to read next frame
        NotGetIFrame,      // I-Frame not obtained after opening stream
        ResolutionChange,  // Stream resolution changed
    };

    class VideoDemuxer {
    public:
        VideoDemuxer();
        ~VideoDemuxer();

        void SetFile(const std::string&);
        void SetForceFps(float fps);
        void CloseStream(bool repeat = false);

        int GetWidth() const {
            return width_.load(std::memory_order_relaxed);
        }
        int GetHeight() const {
            return height_.load(std::memory_order_relaxed);
        }
        float GetFPS() const {
            return fps_.load(std::memory_order_relaxed);
        }
        VideoCodecType GetEncodeType() const {
            return enc_type_;
        }

        bool IsLiveStream() const {
            return strategy_ && strategy_->IsLive();
        }

        // Open stream. Can be used to verify online status
        util::ErrorEnum OpenStream(bool repeat = false, int pullTimeoutSec = 5, int delayMs = 200);
        util::ErrorEnum FindStream(bool repeat = false);  // Get stream
        ReadFrameStatus Demux(VideoPacketPtr pkt);

    private:
        // Frame rate control for local files
        void LocalFileFpsCtrl();
        // ffmpeg read 1 frame
        int AvReadFrame(AVPacket* packet);
        // Calculate real frame rate
        void CalcFps(AVPacket* packet);
        // Calculate real frame index
        void CalcFrameIndex(AVPacket* packet);
        void SafeCloseContext();
        // Initialize BSF context for the given codec type
        bool InitBsfContext();

        void ResetStreamState();
        std::unique_ptr<IDemuxStrategy> CreateStrategy(const std::string& file, int pullTimeoutSec,
                                                       int delayMs);

    private:
        std::string filename_;
        VideoCodecType enc_type_{VideoCodecType::kH264};
        AVFormatContext* fmt_ctx_{nullptr};

        AVBSFContext* bsf_ctx_{nullptr};

        int video_stream_idx_;

        std::mutex mutex_;
        std::atomic<int> width_{0};
        std::atomic<int> height_{0};
        std::atomic<float> fps_{0};
        float video_force_fps_{-1.0};  // Forced stream reading frame rate
        std::condition_variable cond_;
        int64_t duration_active_{-1};            // Frame duration used to calculate frame index
        int64_t duration_real_time_{100000000};  // Frame duration statistics. If same for 3 consecutive
                                                 // times, assigned to duration_active
        int duration_active_count_{0};           // Count of consecutive same duration_real_time
        int64_t pts_{0};                         // Previous frame's pts
        int64_t stream_opened_cnt_{0};           // Number of times stream opened
        int64_t frame_index_{0};
        int64_t time_diff_count_{0};
        int64_t packet_index_{0};
        int64_t abs_packet_index_{0};
        std::chrono::steady_clock::time_point open_time_point_;  // For local file frame rate control

        int64_t fps_calc_frame_{0};                               // For calculating frame rate
        std::chrono::steady_clock::time_point start_time_point_;  // For calculating frame rate
        int64_t start_pts_{0};                                    // For calculating frame rate

        std::unique_ptr<IDemuxStrategy> strategy_;
        bool opened_{false};
        bool ready_{false};
        bool end_{false};

        bool key_frame_detected_;
    };

}  // namespace media
}  // namespace cosmo
