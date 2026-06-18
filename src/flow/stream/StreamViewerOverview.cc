// StreamViewerOverview.cc — Core coordination for StreamViewerOverview.
// Manages frame lifecycle, cache management, and orchestration of
// data collection (StreamViewerLiveData.cc) and rendering (StreamViewerDraw.cc).

#include "flow/stream/StreamViewerOverview.h"

#include <algorithm>
#include <chrono>

#include "flow/common/FlowTaskUtil.h"
#include "flow/stream/StreamViewerOverviewTypes.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/task/ITaskQuery.h"
#include "util/Log.h"

namespace cosmo {

StreamViewerOverview::~StreamViewerOverview() {
    LOG_INFO("{} Alg:{} Delete", channel_id_, alg_id_);
}

StreamViewerOverview::StreamViewerOverview(const std::string& channelId, const std::string& algId)
    : channel_id_(std::move(channelId)), alg_id_(std::move(algId)) {
    task_id_         = ChannelAlgIdToTaskId(channel_id_, alg_id_);
    bool bLiveStream = false;
    int64_t index = 0, pts = 0, frameSize = 0;
    std::string streamUrl;
    service::ServiceRegistry::Instance().Get<service::ITaskQuery>().GetTaskFrameInfo(
        task_id_, bLiveStream, index, pts, frameSize, streamUrl);
    // This class is only for algorithm task preview: overlays use relaxed frame alignment/time window,
    // cannot rely on demuxer IsLiveStream() (historically only rtsp:// was live, rtmp was treated as VOD).
    live_stream_ = true;
    LOG_INFO("{} Alg:{} Task:{} live_stream_:true Init (channel demuxer reports live:{})", channel_id_,
             alg_id_, task_id_, bLiveStream);
}

VideoFramePtr StreamViewerOverview::FrameCache(VideoFramePtr frame) {
    frames_.push_back(frame);

    if ((frames_.front()->GetTimestamp() + frame_duration_ > frame->GetTimestamp()) &&
        (frames_.size() < frame_size_)) {
        return nullptr;
    }

    auto outFrame = std::move(frames_.front());
    frames_.pop_front();

    return outFrame;
}

VideoFramePtr StreamViewerOverview::HandFrame(VideoFramePtr frame) {
    debug_info_.recvFrames += 1;
    // no overlay algorithm
    if (alg_id_.empty()) {
        return frame;
    }

    // invalid frame
    if (!VideoFrameValid(frame)) {
        return frame;
    }

    fresh_frame_identity_.index       = frame->GetFrameIndex();
    fresh_frame_identity_.streamIndex = frame->GetStreamIndex();
    fresh_frame_identity_.timestamp   = frame->GetTimestamp();
    // Plan C: skip FrameCache wait window, process current frame directly
    // overlay uses latest available AI result (guaranteed by GetOverviewDataFromLocal timestamp fallback)
    auto overViewFrame =
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().CopyJpegSrcFrame(frame);
    if (!VideoFrameValid(overViewFrame)) {
        return nullptr;
    }
    frame_identity_.index       = overViewFrame->GetFrameIndex();
    frame_identity_.streamIndex = overViewFrame->GetStreamIndex();
    frame_identity_.timestamp   = overViewFrame->GetTimestamp();
    auto osdStart               = std::chrono::steady_clock::now();
    HandOverview(overViewFrame);
    auto osdEnd = std::chrono::steady_clock::now();
    osd_duration_.duration_ns +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(osdEnd - osdStart).count();
    osd_duration_.count += 1;
    debug_info_.sendFrames += 1;
    return overViewFrame;
}

void StreamViewerOverview::HandOverview(VideoFramePtr frame) {
    if (live_stream_) {
        HandLiveOverview(frame);
    } else {
        HandVodOverview(frame);
    }
    hand_data_count_++;
}

void StreamViewerOverview::OldLocalData() {
    // clean up expired alarm overlay data (common for live/VOD)
    infos_.alarmOverviews.erase(std::remove_if(infos_.alarmOverviews.begin(), infos_.alarmOverviews.end(),
                                               [&](const StreamOverviewAlarmInfo& a) {
                                                   return (a.timestamp + 3000) < frame_identity_.timestamp;
                                               }),
                                infos_.alarmOverviews.end());

    if (live_stream_) {
        while (infos_.overviews.size() > frame_size_) {
            infos_.overviews.erase(infos_.overviews.begin());
        }
        // clean up expired EMA smooth cache (time-based, frame-rate independent)
        for (auto it = smoothed_boxes_.begin(); it != smoothed_boxes_.end();) {
            if (frame_identity_.timestamp > it->second.lastSeenTimestamp + kSmoothExpireMs) {
                it = smoothed_boxes_.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }

    if (infos_.overviews.size() < 2) {
        return;
    }

    // find the starting position to keep
    auto cutIt        = infos_.overviews.begin();
    bool foundCurrent = false;

    for (auto it = infos_.overviews.begin(); it != infos_.overviews.end(); ++it) {
        // data with different streamIndex should be removed
        if (it->streamIndex != frame_identity_.streamIndex) {
            continue;
        }
        // found current frame or later data
        if (it->index >= frame_identity_.index) {
            foundCurrent = true;
            cutIt        = it;
            break;
        }
        // not yet at current frame, record last old frame position
        cutIt = it;
    }

    // batch remove all elements before cutIt + elements with different streamIndex
    auto newEnd =
        std::remove_if(infos_.overviews.begin(), infos_.overviews.end(), [&](const StreamOverviewEl& el) {
            if (el.streamIndex != frame_identity_.streamIndex) {
                return true;  // different stream - remove directly
            }
            if (foundCurrent && el.index < cutIt->index) {
                return true;  // before current frame - remove
            }
            return false;
        });
    infos_.overviews.erase(newEnd, infos_.overviews.end());
}

void StreamViewerOverview::HandLiveOverview(VideoFramePtr frame) {
    if (0 == hand_data_count_ % 3) {
        width_  = frame->GetWidth();
        height_ = frame->GetHeight();
        LiveDataToLocal();
        OldLocalData();
    }

    auto info = GetOverviewDataFromLocal(frame);

    // single BeginOSD, all drawing shares the same bm_image
    service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().BeginOSD(frame);
    FrameOverview(frame, info);
    DrawActionDuration(frame);
    service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().EndOSD();
}

void StreamViewerOverview::HandVodOverview(VideoFramePtr frame) {
    HandLiveOverview(frame);
}

}  // namespace cosmo