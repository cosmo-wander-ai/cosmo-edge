// AlgChannelDemuxStream.cc — Stream management for AlgChannelDemux.
// Split from AlgChannelDemux.cc to reduce file size (DEBT-007).

#include "flow/channel/AlgChannelDemux.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IEventNotifier.h"
#include "service/system/IConfigReadService.h"
#include "service/task/ITaskChannel.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/TimingConstants.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/CameraMsgTypes.h"

static constexpr const char* kTag = "ChannelDemux ";
namespace chrono                  = std::chrono;
namespace cosmo {

void AlgChannelDemux::NotifyOnInfo() {
    if (!service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        return;
    }

    auto tasks =
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelTasks(channel_id_);
    CMsgOnInfoReq req;
    CMsgonInfoRsp rsp;
    auto encType              = demuxer_.GetEncodeType();
    auto fps                  = demuxer_.GetFPS();
    auto width                = demuxer_.GetWidth();
    auto height               = demuxer_.GetHeight();
    std::string strFps        = std::to_string(fps);
    std::string resolution    = std::to_string(width) + "*" + std::to_string(height);
    std::string videoEncoding = "unknown";
    if (encType == media::VideoCodecType::kH264) {
        videoEncoding = "H.264";
    } else if (encType == media::VideoCodecType::kH265) {
        videoEncoding = "H.265";
    } else if (encType == media::VideoCodecType::kMjpeg) {
        videoEncoding = "MJPEG";
    }

    for (const auto& task : tasks) {
        CMsgOnInfoDetail detail;
        detail.taskId         = task;
        detail.videoChannelId = channel_id_;

        detail.resolution    = resolution;
        detail.videoEncoding = videoEncoding;
        detail.fps           = strFps;
        req.details.push_back(detail);
    }
    LOG_INFO("{}:{} OnInfo", kTag, channel_id_);
    service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().NotifyInfo(req, rsp);
}

void AlgChannelDemux::SetStatusInfo(service::camera::AlgDemuxStatus status) {
    if (status != status_.status) {
        auto now = chrono::steady_clock::now();
        LOG_INFO("{}{} read {} From {} To {} Cost {} ms", kTag, channel_id_, url_,
                 StatusString(status_.status), StatusString(status),
                 chrono::duration_cast<chrono::milliseconds>(now - status_.timePoint).count());
        status_.status    = status;
        status_.timePoint = now;
    }
}

void AlgChannelDemux::RequestUrl() {
    if (!service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        return;
    }

    CMsgGetVideoPlayReq getVideoReq;
    CMsgGetVideoPlayRsp getVideoRsp;
    if (poll_channel_id_.empty()) {
        getVideoReq.videoChannelId = channel_id_;
    } else {
        getVideoReq.videoChannelId = poll_channel_id_;
    }
    if (!service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().GetVideoPlayUrl(
            getVideoReq, getVideoRsp)) {
        LOG_WARN("{}{} GetVideoPlay Failed", kTag, channel_id_);
    } else {
        LOG_INFO("{}{} GetVideoPlay {}", kTag, channel_id_, getVideoRsp.resData.streamUrl);
        SetUrl(getVideoRsp.resData.streamUrl);
    }
}

std::string AlgChannelDemux::StatusString(service::camera::AlgDemuxStatus status) {
    switch (status) {
        case service::camera::AlgDemuxStatus::AlgDemuxInit:
            return "Init";
        case service::camera::AlgDemuxStatus::AlgDemuxInvalidUrl:
            return "InvalidUrl";
        case service::camera::AlgDemuxStatus::AlgDemuxOpened:
            return "Opened";
        case service::camera::AlgDemuxStatus::AlgDemuxOpenFailed:
            return "OpenFailed";
        case service::camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized:
            return "Unauthorized";
        case service::camera::AlgDemuxStatus::AlgDemuxReading:
            return "Reading";
        case service::camera::AlgDemuxStatus::AlgDemuxReadEnd:
            return "ReadEnd";
        case service::camera::AlgDemuxStatus::AlgDemuxReadFailed:
            return "ReadFailed";
        case service::camera::AlgDemuxStatus::AlgDemuxClosed:
            return "Closed";
        default:
            return "Unknow";
    }
}

bool AlgChannelDemux::GetAttr(MsgCameraAttr& attr) {
    attr.width  = demuxer_.GetWidth();
    attr.height = demuxer_.GetHeight();
    attr.fps    = demuxer_.GetFPS();
    auto codec  = demuxer_.GetEncodeType();
    if (media::VideoCodecType::kH265 == codec) {
        attr.codec = "H265";
    } else if (media::VideoCodecType::kH264 == codec) {
        attr.codec = "H264";
    } else if (media::VideoCodecType::kMjpeg == codec) {
        attr.codec = "MJPEG";
    } else {
        attr.codec = "";
    }

    if (service::camera::AlgDemuxStatus::AlgDemuxReading == status_.status) {
        attr.channelStatus = ChannelStatus::ChannelStatusOnline;
    } else if (service::camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized == status_.status) {
        attr.channelStatus = ChannelStatus::ChannelStatusUnauthorized;
    } else {
        attr.channelStatus = ChannelStatus::ChannelStatusOffline;
    }

    // VoD stream that finished reading is still considered online.
    if (!IsLiveStream()) {
        if (service::camera::AlgDemuxStatus::AlgDemuxReadEnd == status_.status) {
            attr.channelStatus = ChannelStatus::ChannelStatusOnline;
        }
    }
    if (ChannelStatus::ChannelStatusOnline == attr.channelStatus) {
        if (!media::IsValidVideoResolution(attr.width, attr.height)) {
            attr.channelStatus = ChannelStatus::ChannelStatusResolusionUnSupport;
        }
    }
    attr.dataStatus = static_cast<int>(status_.status);
    return true;
}

bool AlgChannelDemux::RecordMp4(RecordParam& recordParam) {
    return recorder_->RecordMp4(recordParam);
}

bool AlgChannelDemux::IsDataActive() const {
    // Frame sequence changed; data is arriving.
    if (check_active_frame_ != read_frames_) {
        check_active_frame_ = read_frames_;
        check_active_ts_    = util::GetMillisecondsFromSteadyClock();
        return true;
    }

    // Frame sequence unchanged.
    auto now = util::GetMillisecondsFromSteadyClock();
    // Same sequence for >500ms; no data.
    if (now - check_active_ts_ > 500) {
        action_status_ = util::ErrorEnum::DemuxNoData;
        return false;
    }
    // Data just stopped; timestamp not updated. Next check enters no-data path.
    return true;
}

void AlgChannelDemux::run() {
    bool streamOpened = false;
    while (is_running_) {
        // Stream open or read failed; need to re-fetch URL.
        if (((service::camera::AlgDemuxStatus::AlgDemuxOpenFailed == status_.status) ||
             (service::camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized == status_.status) ||
             (service::camera::AlgDemuxStatus::AlgDemuxReadFailed == status_.status) ||
             (service::camera::AlgDemuxStatus::AlgDemuxInvalidUrl == status_.status))) {
            // Invalid stream URL.
            if ((service::camera::AlgDemuxStatus::AlgDemuxInvalidUrl == status_.status) ||
                (service::camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized == status_.status)) {
                RequestUrl();
            } else if (read_frames_ > 1000)  // Read failed after a stable stream.
            {
                RequestUrl();
            } else {
                if (open_failed_count_ > 60) {
                    RequestUrl();
                }
            }
        }

        if (!streamOpened) {
            streamOpened = OpenStream();
            if (!streamOpened) {
                std::this_thread::sleep_for(timing::kOneSecondInterval);
                if (0 == open_failed_count_ % 600) {
                    LOG_INFO("{}:{} OpenStream:{} Failed, Status:{} FailedCount:{}", kTag, channel_id_, url_,
                             StatusString(status_.status), open_failed_count_);
                }
                open_failed_count_ += 1;
                continue;
            }
            open_failed_count_ = 0;
        }

        if (is_url_changed_) {
            LOG_INFO("{}:{} CloseStream URL Change", kTag, channel_id_);
            CloseStream();
            streamOpened = false;
            continue;
        }

        if (is_need_repeat_) {
            LOG_INFO("{}:{} Need Repeat", kTag, channel_id_);
            streamOpened = false;
            continue;
        }

        HandleStream();
    }

    LOG_INFO("THREAD [{}] Stop ", Name());
}

void AlgChannelDemux::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus,
                                  unsigned int /*durationSec*/) {
    AlgActionDataQueueStatus status;
    status.actionStatus = action_status_;
    status.actionId     = std::string(BAStreamChannel_Code) + " DEMUX";
    queStatus.push_back(status);
}

void AlgChannelDemux::ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) {
    ActionRuntimeInfo actionInfoEl;
    actionInfoEl.actionId = std::string(BAStreamChannel_Code) + " DEMUX";
    auto bindTasks        = GetBindTasks();
    for (auto& bindTask : bindTasks) {
        for (auto& task : bindTask.tasks) {
            ActionRuntimeSon son;
            son.channelId = task.channel_id;
            son.taskId    = task.task_id;
            son.actionId  = task.actionId;
            actionInfoEl.sons.push_back(son);
        }
    }
    actionInfos.push_back(actionInfoEl);
}

VideoPacketPtr AlgChannelDemux::GetLastFrame() const {
    std::shared_lock<std::shared_mutex> lock(demux_mtx_);
    return last_frame_;
}

bool AlgChannelDemux::IsLiveStream() const {
    return demuxer_.IsLiveStream();
}

void AlgChannelDemux::ClearLastFrame() {
    std::lock_guard<std::shared_mutex> lock(demux_mtx_);
    last_frame_.reset();
}

}  // namespace cosmo
