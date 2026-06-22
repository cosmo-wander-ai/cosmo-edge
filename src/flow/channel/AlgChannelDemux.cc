// AlgChannelDemux — channel demuxer lifecycle and stream distribution.

#include "flow/channel/AlgChannelDemux.h"

#include <algorithm>

#include "service/detail/ServiceRegistry.h"
#include "service/event/IEventNotifier.h"
#include "service/system/IConfigReadService.h"
#include "service/task/ITaskChannel.h"
#include "util/Log.h"
#include "util/TimeUtil.h"
#include "util/TimingConstants.h"
#include "util/dto/ActionCodes.h"

namespace chrono = std::chrono;

static constexpr const char* kTag = "ChannelDemux ";
namespace cosmo {

AlgChannelDemux::~AlgChannelDemux() {
    LOG_INFO("{}:{} Url:{} Delete", kTag, channel_id_, url_);
    ClearLastFrame();
}

AlgChannelDemux::AlgChannelDemux(const std::string& channel_id, const std::string& url)
    : AlgDataQueueDistributor(channel_id + " Demux"),
      util::Thread(channel_id + " Demux"),
      channel_id_(channel_id),
      url_(url),
      recorder_(std::make_shared<AlgChannelMp4>(channel_id)),
      duration_stat_(channel_id + " Demux") {
    action_status_    = util::ErrorEnum::ActionReady;
    status_.status    = service::camera::AlgDemuxStatus::AlgDemuxInit;
    status_.timePoint = chrono::steady_clock::now();
    LOG_INFO("{}:{} Url:{} Init", kTag, channel_id_, url_);
}

bool AlgChannelDemux::SetUrl(const std::string& url) {
    if (url.empty())
        return false;
    if (url_ != url) {
        LOG_INFO("{}:{} Url Change From {} To {}", kTag, channel_id_, url_, url);
        url_              = url;
        is_url_changed_   = true;
        video_read_count_ = 0;
        ClearLastFrame();
    }
    return true;
}

bool AlgChannelDemux::SetVideoRepeatCount(int repeat_count) {
    if (video_repeat_count_ != repeat_count) {
        LOG_INFO("{}:{} VideoRepeatCount Change From {} To {}", kTag, channel_id_, video_repeat_count_,
                 repeat_count);
        video_repeat_count_ = repeat_count;
    }
    return true;
}

bool AlgChannelDemux::SetForStartTask() {
    if ((!IsLiveStream()) && (service::camera::AlgDemuxStatus::AlgDemuxReadEnd == status_.status)) {
        video_read_count_ = 0;
        is_need_repeat_   = true;
    }
    return true;
}

void AlgChannelDemux::AddViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue) {
    std::lock_guard<std::shared_mutex> lock(demux_mtx_);
    if (!async_packet_queue) {
        return;
    }
    auto it = std::find(async_packet_queues_.begin(), async_packet_queues_.end(), async_packet_queue);
    if (it == async_packet_queues_.end()) {
        async_packet_queues_.push_back(async_packet_queue);
    }
}

void AlgChannelDemux::RemoveViewerPacketQueue(
    std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue) {
    std::lock_guard<std::shared_mutex> lock(demux_mtx_);
    async_packet_queues_.erase(
        std::remove(async_packet_queues_.begin(), async_packet_queues_.end(), async_packet_queue),
        async_packet_queues_.end());
}

bool AlgChannelDemux::SetVideoFps(float fps) {
    demuxer_.SetForceFps(fps);
    return true;
}

bool AlgChannelDemux::SetPollChannel(const std::string& channel_id) {
    if (poll_channel_id_ != channel_id) {
        LOG_INFO("{}:{} PollChannel Change From {} To {}", kTag, channel_id_, poll_channel_id_, channel_id);
        poll_channel_id_ = channel_id;
    }
    return true;
}

void AlgChannelDemux::Start() {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    if (is_running_.exchange(true)) {
        LOG_INFO("{}:{} Url:{} Already Running", kTag, channel_id_, url_);
        return;
    }
    ResetDistributor();
    if (!start()) {
        is_running_    = false;
        action_status_ = util::ErrorEnum::ActionStop;
        LOG_ERRO("{}:{} Url:{} Start failed: previous thread still joinable", kTag, channel_id_, url_);
        return;
    }
    action_status_ = util::ErrorEnum::ActionStart;
}

void AlgChannelDemux::Stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    LOG_INFO("{}:{} Wait To Stop", kTag, channel_id_);
    if (!is_running_.exchange(false)) {
        LOG_INFO("{}:{} Url:{} Already Stopped", kTag, channel_id_, url_);
        return;
    }
    stop();
    // Reset is_need_repeat_ before CloseStream. Otherwise
    // CloseStream → demuxer_.CloseStream(is_need_repeat_=true) skips closing the
    // FFmpeg context (repeat mode keeps it open for local files), causing the
    // old context to leak on the next OpenStream.
    is_need_repeat_   = false;
    video_read_count_ = 0;
    CloseStream();
    ClearLastFrame();
    action_status_ = util::ErrorEnum::ActionStop;
}

bool AlgChannelDemux::OpenStream() {
    if (url_.size() < 7) {
        action_status_ = util::ErrorEnum::DemuxOpenInvalidUrl;
        SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxInvalidUrl);
        return false;
    }

    // Skip retry for unauthorized status unless URL changed or 30 min elapsed.
    if (service::camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized == status_.status) {
        if (!is_url_changed_) {
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::seconds>(now - status_.timePoint).count() < 30 * 60) {
                return false;
            }
        }
    }
    demuxer_.SetFile(url_);
    read_frames_    = 0;
    is_url_changed_ = false;
    auto ret        = demuxer_.OpenStream(is_need_repeat_);
    if (util::ErrorEnum::Success != ret) {
        if (util::ErrorEnum::DemuxOpenStreamUnauthorized == ret) {
            action_status_ = util::ErrorEnum::DemuxOpenStreamUnauthorized;
            SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized);
        } else {
            action_status_ = util::ErrorEnum::DemuxOpenStreamFail;
            SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxOpenFailed);
        }
        return false;
    }

    if (util::ErrorEnum::Success != demuxer_.FindStream(is_need_repeat_)) {
        action_status_ = util::ErrorEnum::DemuxGetStreamFail;
        SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxOpenFailed);
        return false;
    }
    recorder_->SetFps(demuxer_.GetFPS());

    NotifyOnInfo();
    video_read_count_ += 1;

    is_need_repeat_ = false;
    is_have_report_ = false;
    is_opened_      = true;
    read_frames_    = 0;
    SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxOpened);
    action_status_ = util::ErrorEnum::DemuxStreamStart;
    LOG_INFO("{}{} Stream Opened", kTag, channel_id_);
    return true;
}

void AlgChannelDemux::CloseStream() {
    if (is_opened_) {
        is_opened_ = false;
        demuxer_.CloseStream(is_need_repeat_);
        action_status_ = util::ErrorEnum::DemuxStreamClosed;
        SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxClosed);
        ClearLastFrame();
    }
}

void AlgChannelDemux::HandleStream() {
    // Return early if stream is not opened or actively reading.
    if (!((service::camera::AlgDemuxStatus::AlgDemuxOpened == status_.status) ||
          (service::camera::AlgDemuxStatus::AlgDemuxReading == status_.status) ||
          (service::camera::AlgDemuxStatus::AlgDemuxReadFailed == status_.status))) {
        std::this_thread::sleep_for(timing::kOneSecondInterval);
        no_data_count_++;
        if (0 == no_data_count_ % 600) {
            LOG_INFO("{}:{} No Data Status Is {}", kTag, channel_id_, StatusString(status_.status));
        }
        return;
    }
    no_data_count_    = 0;
    auto frame_packet = std::make_shared<media::VideoPacket>();
    duration_stat_.BeginSample();
    auto ret = demuxer_.Demux(frame_packet);
    duration_stat_.EndSample();
    if (media::ReadFrameStatus::Success != ret) {
        if (media::ReadFrameStatus::StreamEnd == ret) {
            if ((video_read_count_ < video_repeat_count_) || (video_repeat_count_ <= 0)) {
                LOG_INFO("{}:{} Stream End Now RepeatCount:{} Max RepeatCount:{}", kTag, channel_id_,
                         video_read_count_, video_repeat_count_);
                is_need_repeat_ = true;
            } else {
                // Do not report completion for live streams.
                if ((!is_have_report_) && (!IsLiveStream())) {
                    is_have_report_ = true;
                    NotifyOnComplete();
                }
            }
            if (IsLiveStream()) {
                LOG_INFO("{}:{} Live Stream End  Need ReOpen Or Request Url", kTag, channel_id_);
                // Live stream has no definitive end — request new URL or reopen.
                SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxReadFailed);
                is_need_repeat_ = true;
                action_status_  = util::ErrorEnum::DemuxReadStreamFail;
            } else {
                SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxReadEnd);
                action_status_ = util::ErrorEnum::DemuxStreamClosed;

                // Signal unfinished recording tasks to finalize.
                frame_packet->index = -1;
                recorder_->TaskFrame(frame_packet);
            }
            return;
        }
        SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxReadFailed);
        action_status_ = util::ErrorEnum::DemuxReadStreamFail;
        return;
    }
    SetStatusInfo(service::camera::AlgDemuxStatus::AlgDemuxReading);

    read_frames_++;
    AlgDataPtr data           = std::make_shared<AlgData>();
    data->dataType            = AlgDataType::ChannelDataOrig;
    data->chanDataOrig.packet = frame_packet;
    data->chanDataOrig.fps    = demuxer_.GetFPS();
    data->firstTimePoint      = frame_packet->time_point;
    action_status_            = util::ErrorEnum::Success;
    if (recorder_) {
        recorder_->TaskFrame(frame_packet);
    }
    {
        // Distribute to display viewer.
        std::shared_lock<std::shared_mutex> lock(demux_mtx_);
        for (const auto& async_packet_queue : async_packet_queues_) {
            if (async_packet_queue) {
                async_packet_queue->Insert(frame_packet);
            }
        }
    }
    {
        std::lock_guard<std::shared_mutex> lock(demux_mtx_);
        if (frame_packet) {
            last_frame_ = frame_packet;
        }
    }

    DistributorData(data);
}

void AlgChannelDemux::NotifyOnComplete() {
    if (!service::ServiceRegistry::Instance().Get<service::IConfigReadService>().IsNetworkModel()) {
        return;
    }

    auto tasks =
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelTasks(channel_id_);
    for (const auto& task : tasks) {
        CMsgOnCompleteReq req;
        CMsgOnCompleteRsp rsp;
        req.taskId = task;
        LOG_INFO("{}:{} {} Complete", kTag, channel_id_, task);
        service::ServiceRegistry::Instance().Get<cosmo::service::IEventNotifier>().NotifyComplete(req, rsp);
    }
}

// Stream management — moved to AlgChannelDemuxStream.cc

}  // namespace cosmo
