#pragma once

#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "flow/stream/RtmpStreamPusher.h"
#include "media/VideoEncoder.h"
#include "media/VideoFrame.h"
#include "util/AsyncQueue.h"
#include "util/DurationStat.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"

namespace cosmo {

class StreamViewerEncoder {
public:
    StreamViewerEncoder(media::VideoCodecType videoType, int width, int height,
                        RtmpStreamPusherPtr videoPusher);
    ~StreamViewerEncoder();

    bool HandFrame(VideoFramePtr frame);
    bool IsOpened() const {
        return encoder_ != nullptr;
    }
    MsgOverviewDebugInfo GetProcInfo() {
        return debug_info_;
    }
    util::DurationStatInfo GetDurationInfo(int durationMs = 5000) {
        return duration_stat_.ComputeStats(durationMs);
    }

private:
    bool OpenEncoder();
    bool ContainsKeyFrame(const uint8_t* data, size_t size) const;
    bool IsSmallStartupKeyFrame(const uint8_t* data, size_t size) const;
    void ProcFrame(VideoFramePtr frame);

    std::mutex mtx_;
    MsgOverviewDebugInfo debug_info_;
    media::VideoCodecType video_type_{media::VideoCodecType::kInvalid};
    int width_{0};
    int height_{0};
    int startup_small_key_frame_count_{0};
    bool startup_key_frame_accepted_{false};
    std::shared_ptr<media::VideoEncoder> encoder_ = nullptr;
    RtmpStreamPusherPtr video_pusher_{nullptr};
    AsyncQueue<VideoFramePtr> async_queue_;
    util::DurationStat duration_stat_;
};
using StreamViewerEncoderPtr = std::shared_ptr<StreamViewerEncoder>;
}  // namespace cosmo
