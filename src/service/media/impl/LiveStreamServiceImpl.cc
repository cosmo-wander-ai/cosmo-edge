// LiveStreamService implementation
// Business logic migrated from flow/stream/StreamViewerMng.

#include "service/media/impl/LiveStreamServiceImpl.h"

#include <algorithm>
#include <thread>
#include <utility>

#include "flow/stream/StreamViewer.h"
#include "media/PreviewPipelineMetrics.h"
#include "service/camera/ICameraChannelQuery.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/detail/ServiceRegistry.h"
#include "util/EnvUtil.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/ProcessShutdown.h"
#include "util/TimingConstants.h"
#include "util/dto/CameraMsgTypes.h"

namespace cosmo::service {

namespace {
    // Stream connection constants
    static constexpr int kDefaultKeepAliveInterval = 10;
    static constexpr const char* kKeepAliveUrl     = "streamkeepalive";
    static constexpr int kDefaultHttpPort          = 8080;
    static constexpr int kDefaultRtcApiPort        = 1985;
    static constexpr auto kRawStreamReadyTimeout   = std::chrono::milliseconds(5000);
    static constexpr auto kAlgStreamReadyTimeout   = std::chrono::milliseconds(15000);

    std::string BuildViewerKey(const std::string& channelId, const std::string& algCode) {
        return COSMO_FORMAT("{}\n{}", channelId, algCode);
    }

    std::chrono::milliseconds StreamReadyTimeout(const std::string& algCode) {
        return algCode.empty() ? kRawStreamReadyTimeout : kAlgStreamReadyTimeout;
    }

    bool IsChannelStartupState(cosmo::util::ErrorEnum state) {
        switch (state) {
            case cosmo::util::ErrorEnum::ActionReady:
            case cosmo::util::ErrorEnum::ActionStart:
            case cosmo::util::ErrorEnum::ActionStop:
            case cosmo::util::ErrorEnum::DemuxStreamStart:
            case cosmo::util::ErrorEnum::DemuxNoData:
                return true;
            default:
                return false;
        }
    }

    bool WaitForChannelReady(const cosmo::AlgChannelPtr& channel, std::chrono::milliseconds timeout,
                             cosmo::util::ErrorEnum& last_state) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do {
            if (util::ProcessShutdown::Requested()) {
                last_state = cosmo::util::ErrorEnum::LiveStreamStopped;
                return false;
            }
            last_state = channel->GetUrlStatus();
            if (last_state == cosmo::util::ErrorEnum::Success) {
                return true;
            }
            if (!IsChannelStartupState(last_state)) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    class PreviewChannelLease {
    public:
        PreviewChannelLease(service::ICameraTaskConfig& camera_service, std::string channel_id)
            : camera_service_(camera_service), channel_id_(std::move(channel_id)) {}

        cosmo::util::ErrorEnum Acquire() {
            if (util::ProcessShutdown::Requested())
                return cosmo::util::ErrorEnum::LiveStreamStopped;
            const auto result = camera_service_.AcquirePreviewChannel(channel_id_);
            acquired_         = result == cosmo::util::ErrorEnum::Success;
            return result;
        }

        void Commit() {
            acquired_ = false;
        }

        ~PreviewChannelLease() {
            if (acquired_) {
                camera_service_.ReleasePreviewChannel(channel_id_);
            }
        }

    private:
        service::ICameraTaskConfig& camera_service_;
        std::string channel_id_;
        bool acquired_{false};
    };

    void StopViewerAndReleasePreview(const cosmo::StreamViewerPtr& viewer) {
        if (!viewer) {
            return;
        }
        const auto channel_id = viewer->GetChannelId();
        viewer->Stop();
        service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>().ReleasePreviewChannel(
            channel_id);
    }

    std::string BuildStreamName(const std::string& channelId, const std::string& algCode) {
        return algCode.empty() ? channelId : COSMO_FORMAT("{}_{}", channelId, algCode);
    }

    void PopulateStreamInfo(LiveStream::LiveStreamInfo& info, const std::string& channelId,
                            const std::string& algCode) {
        const std::string streamName = BuildStreamName(channelId, algCode);
        const std::string playMode   = util::GetEnvOrDefault("COSMO_STREAM_PLAY_MODE", "srs");

        info.httpPort          = util::GetEnvIntOrDefault("COSMO_STREAM_HTTP_PORT", kDefaultHttpPort);
        info.rtcApiPort        = util::GetEnvIntOrDefault("COSMO_STREAM_RTC_API_PORT", kDefaultRtcApiPort);
        info.webrtcUrl         = COSMO_FORMAT("/rtc/v1/whep/?app={}&stream={}", "live", streamName);
        info.flvUrl            = COSMO_FORMAT("/live/{}.flv", streamName);
        info.hlsUrl            = COSMO_FORMAT("/live/{}.m3u8", streamName);
        info.keepAliveInterval = kDefaultKeepAliveInterval;
        info.keepAliveUrl      = kKeepAliveUrl;

        if (playMode == "srs" || playMode == "webrtc") {
            info.protocol = "webrtc";
            info.port     = info.rtcApiPort;
            info.url      = info.webrtcUrl;
        } else if (playMode == "srs-flv" || playMode == "httpflv-srs") {
            info.protocol = "httpflv";
            info.port     = info.httpPort;
            info.url      = info.flvUrl;
        } else {
            info.protocol = "httpflv";
            info.port     = info.httpPort;
            info.url      = COSMO_FORMAT("/live?app={}&stream={}", "live", streamName);
        }
    }
}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LiveStreamServiceImpl::LiveStreamServiceImpl() : is_running_(true) {
    watchdog_thread_ = std::thread(&LiveStreamServiceImpl::HeartBeatWatchdog, this);
    LOG_INFO("{}", "LiveStreamServiceImpl Init");
}

LiveStreamServiceImpl::~LiveStreamServiceImpl() {
    LiveStreamServiceImpl::Stop();
    LOG_INFO("{}", "LiveStreamServiceImpl Delete");
}

void LiveStreamServiceImpl::Stop() {
    std::lock_guard<std::mutex> stop_lock(stop_mtx_);
    if (stopped_) {
        return;
    }
    stopping_.store(true, std::memory_order_release);

    // Wait for any request that entered before stopping_ was published. New
    // requests take a shared lock, observe stopping_, and fail without work.
    std::unique_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    is_running_.store(false, std::memory_order_release);
    watchdog_cv_.notify_all();
    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }

    std::vector<cosmo::StreamViewerPtr> viewers_to_stop;
    std::vector<std::string> orphaned_channel_leases;
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        viewers_to_stop.swap(viewers_);
        for (auto& [key, gate] : starting_viewers_) {
            (void)key;
            gate->cancelled = true;
            gate->finished  = true;
            gate->result    = cosmo::util::ErrorEnum::LiveStreamStopped;
            if (gate->viewer) {
                viewers_to_stop.push_back(gate->viewer);
                gate->viewer.reset();
            } else if (gate->channel_lease_acquired) {
                orphaned_channel_leases.push_back(gate->channel_id);
            }
            gate->channel_lease_acquired = false;
            gate->cv.notify_all();
        }
        starting_viewers_.clear();
    }
    for (auto& viewer : viewers_to_stop) {
        StopViewerAndReleasePreview(viewer);
    }
    for (const auto& channel_id : orphaned_channel_leases) {
        service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>().ReleasePreviewChannel(
            channel_id);
    }
    stopped_ = true;
    LOG_INFO("{}", "LiveStreamServiceImpl Delete");
}

// ---------------------------------------------------------------------------
// HeartBeat watchdog thread — replaces StreamViewerMng::run()
// ---------------------------------------------------------------------------

void LiveStreamServiceImpl::HeartBeatWatchdog() {
    int index = 0;
    std::unique_lock<std::mutex> wait_lock(watchdog_mtx_);
    while (is_running_.load(std::memory_order_acquire)) {
        wait_lock.unlock();
        // Check every 10 seconds for viewers without heartbeat and close them
        if (0 == index % 10) {
            CheckAliveTasks();
        }
        index++;
        wait_lock.lock();
        watchdog_cv_.wait_for(wait_lock, timing::kOneSecondInterval,
                              [this]() { return !is_running_.load(std::memory_order_acquire); });
    }
    LOG_INFO("{}", "LiveStreamServiceImpl watchdog thread stopped");
}

// ---------------------------------------------------------------------------
// ViewerCreate
// ---------------------------------------------------------------------------

cosmo::util::ErrorEnum LiveStreamServiceImpl::ViewerCreate(const std::string& channelId,
                                                           const std::string& algCode,
                                                           LiveStream::LiveStreamInfo& streamInfo) {
    std::shared_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopping_.load(std::memory_order_acquire) || util::ProcessShutdown::Requested()) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    const auto request_started_at = std::chrono::steady_clock::now();
    auto channel_inst =
        service::ServiceRegistry::Instance().Get<service::ICameraChannelQuery>().GetChannelInst(channelId);
    if (!channel_inst) {
        return cosmo::util::ErrorEnum::CameraNotExist;
    }
    auto& camera_task_config = service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>();
    if (!algCode.empty()) {
        const auto tasks   = camera_task_config.GetTasks(channelId);
        const auto task_it = std::find_if(tasks.begin(), tasks.end(),
                                          [&](const auto& task) { return task.algorithmCode == algCode; });
        if (task_it == tasks.end()) {
            LOG_WARN("viewer rejected: stream={}/{} task=absent", channelId, algCode);
            return cosmo::util::ErrorEnum::TaskNotExist;
        }
        if (!task_it->enable) {
            LOG_WARN("viewer rejected: stream={}/{} task=stopped", channelId, algCode);
            return cosmo::util::ErrorEnum::ActionStop;
        }
    }

    const auto ready_timeout = StreamReadyTimeout(algCode);
    PreviewChannelLease channel_lease(camera_task_config, channelId);
    const auto lease_result = channel_lease.Acquire();
    if (lease_result != cosmo::util::ErrorEnum::Success) {
        return lease_result;
    }

    cosmo::util::ErrorEnum channel_state = channel_inst->GetUrlStatus();
    if (!WaitForChannelReady(channel_inst, ready_timeout, channel_state)) {
        LOG_WARN("viewer channel startup failed: stream={}/{} state={}", channelId, algCode,
                 cosmo::util::ErrorEnumName(channel_state));
        return cosmo::util::ErrorEnum::DemuxNoData;
    }
    if (util::ProcessShutdown::Requested())
        return cosmo::util::ErrorEnum::LiveStreamStopped;

    cosmo::MsgCameraAttr attr;
    if (!channel_inst->GetAttr(attr)) {
        return cosmo::util::ErrorEnum::CameraNotOnline;
    }
    if (cosmo::ChannelStatus::ChannelStatusOnline != attr.channelStatus) {
        return cosmo::util::ErrorEnum::CameraNotOnline;
    }

    const bool requires_encoder  = !(algCode.empty() && attr.codec == "H264");
    const std::string viewer_key = BuildViewerKey(channelId, algCode);

    std::shared_ptr<ViewerStartGate> gate;
    bool start_owner = false;
    cosmo::StreamViewerPtr failed_viewer;
    {
        // The ready viewer and the startup reservation are checked under one
        // lock. Exactly one request owns publisher construction for a stream;
        // concurrent requests share its result instead of opening duplicate
        // RTMP publishers.
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto ready_it = FindViewer(channelId, algCode);
        if (ready_it != viewers_.end()) {
            if ((*ready_it)->IsPublishReady()) {
                (*ready_it)->UpViewerNum();
                LOG_INFO("viewer reuse: stream={}/{} viewers={} publisher=ready", channelId, algCode,
                         (*ready_it)->GetViewerNum());
                PopulateStreamInfo(streamInfo, channelId, algCode);
                return cosmo::util::ErrorEnum::Success;
            }
            failed_viewer = *ready_it;
            viewers_.erase(ready_it);
            LOG_WARN("viewer reuse rejected: stream={}/{} publisher=failed detail={} release=pending",
                     channelId, algCode, failed_viewer->LastPublishError());
        }

        auto starting_it = starting_viewers_.find(viewer_key);
        if (starting_it != starting_viewers_.end()) {
            gate = starting_it->second;
            gate->participants += 1;
            LOG_INFO("viewer join startup: stream={}/{} waiters={}", channelId, algCode, gate->participants);
        } else {
            if (requires_encoder) {
                const int encoder_count = ViewerEncoderCountLocked();
                if (encoder_count >= view_counts_.load()) {
                    LOG_WARN("viewer rejected: stream={}/{} encoder_limit={} active_or_starting={}",
                             channelId, algCode, view_counts_.load(), encoder_count);
                    return cosmo::util::ErrorEnum::EncodeFailed;
                }
            }
            gate                         = std::make_shared<ViewerStartGate>();
            gate->channel_id             = channelId;
            gate->requires_encoder       = requires_encoder;
            gate->channel_lease_acquired = true;
            starting_viewers_.emplace(viewer_key, gate);
            channel_lease.Commit();
            start_owner = true;
            LOG_INFO("viewer startup reserved: stream={}/{} encoder={}", channelId, algCode,
                     requires_encoder);
        }
    }
    if (failed_viewer) {
        StopViewerAndReleasePreview(failed_viewer);
        LOG_INFO("viewer failed publisher released before restart: stream={}/{}", channelId, algCode);
    }

    if (!start_owner) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        if (!gate->cv.wait_for(lock, ready_timeout, [&gate] { return gate->finished; })) {
            if (gate->participants > 1) {
                gate->participants -= 1;
            }
            LOG_WARN("viewer startup wait timeout: stream={}/{} waiters_remaining={}", channelId, algCode,
                     gate->participants);
            return cosmo::util::ErrorEnum::LiveStreamReadyTimeout;
        }
        const auto result = gate->result;
        lock.unlock();
        if (result == cosmo::util::ErrorEnum::Success) {
            PopulateStreamInfo(streamInfo, channelId, algCode);
        }
        return result;
    }

    auto finish_failure = [&](cosmo::util::ErrorEnum result, const std::string& stage,
                              cosmo::StreamViewerPtr viewer) {
        if (viewer) {
            viewer->Stop();
        }
        bool release_channel_lease = false;
        {
            std::unique_lock<std::shared_mutex> lock(mtx_);
            gate->viewer.reset();
            gate->result                 = result;
            gate->finished               = true;
            release_channel_lease        = gate->channel_lease_acquired;
            gate->channel_lease_acquired = false;
            auto it                      = starting_viewers_.find(viewer_key);
            if (it != starting_viewers_.end() && it->second == gate) {
                starting_viewers_.erase(it);
            }
        }
        if (release_channel_lease) {
            camera_task_config.ReleasePreviewChannel(channelId);
        }
        gate->cv.notify_all();
        cosmo::media::GetPreviewPipelineMetrics().PreviewFailed();
        LOG_ERRO("viewer startup failed: stream={}/{} stage={} error={} publisher=released", channelId,
                 algCode, stage, cosmo::util::ErrorEnumName(result));
    };

    cosmo::StreamViewerPtr viewer;
    if (util::ProcessShutdown::Requested()) {
        finish_failure(cosmo::util::ErrorEnum::LiveStreamStopped, "shutdown", nullptr);
        return cosmo::util::ErrorEnum::LiveStreamStopped;
    }
    try {
        viewer = std::make_shared<cosmo::StreamViewer>(channel_inst, channelId, algCode);
    } catch (const cosmo::util::ErrorMessage& e) {
        const auto result = static_cast<cosmo::util::ErrorEnum>(e.GetValue().value());
        LOG_ERRO("viewer publisher construction failed: stream={}/{} detail={}", channelId, algCode,
                 e.what());
        finish_failure(result, "publisher-connect", nullptr);
        return result;
    } catch (const std::exception& e) {
        LOG_ERRO("viewer construction failed: stream={}/{} detail={}", channelId, algCode, e.what());
        finish_failure(cosmo::util::ErrorEnum::LiveStreamPublishFailed, "viewer-construct", nullptr);
        return cosmo::util::ErrorEnum::LiveStreamPublishFailed;
    }

    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        gate->viewer = viewer;
        if (gate->cancelled || util::ProcessShutdown::Requested()) {
            lock.unlock();
            finish_failure(cosmo::util::ErrorEnum::LiveStreamStopped, "cancelled", viewer);
            return cosmo::util::ErrorEnum::LiveStreamStopped;
        }
    }

    if (!viewer->WaitReady(ready_timeout)) {
        {
            std::shared_lock<std::shared_mutex> lock(mtx_);
            if (gate->cancelled || util::ProcessShutdown::Requested()) {
                lock.unlock();
                finish_failure(cosmo::util::ErrorEnum::LiveStreamStopped, "cancelled", viewer);
                return cosmo::util::ErrorEnum::LiveStreamStopped;
            }
        }
        const std::string publish_error = viewer->LastPublishError();
        const auto result = publish_error.empty() ? cosmo::util::ErrorEnum::LiveStreamReadyTimeout
                                                  : cosmo::util::ErrorEnum::LiveStreamPublishFailed;
        if (!publish_error.empty()) {
            LOG_ERRO("viewer publisher did not become ready: stream={}/{} detail={}", channelId, algCode,
                     publish_error);
        }
        finish_failure(result, publish_error.empty() ? "first-frame-timeout" : "publisher-write", viewer);
        return result;
    }

    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        if (gate->cancelled || util::ProcessShutdown::Requested()) {
            lock.unlock();
            finish_failure(cosmo::util::ErrorEnum::LiveStreamStopped, "cancelled-after-ready", viewer);
            return cosmo::util::ErrorEnum::LiveStreamStopped;
        }
        for (size_t i = 1; i < gate->participants; ++i) {
            viewer->UpViewerNum();
        }
        viewer->MarkReady(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - request_started_at));
        viewers_.push_back(viewer);
        gate->result                 = cosmo::util::ErrorEnum::Success;
        gate->finished               = true;
        gate->channel_lease_acquired = false;
        gate->viewer.reset();
        auto it = starting_viewers_.find(viewer_key);
        if (it != starting_viewers_.end() && it->second == gate) {
            starting_viewers_.erase(it);
        }
        LOG_INFO("viewer startup ready: stream={}/{} viewers={} publisher=ready", channelId, algCode,
                 viewer->GetViewerNum());
    }
    gate->cv.notify_all();

    PopulateStreamInfo(streamInfo, channelId, algCode);
    return cosmo::util::ErrorEnum::Success;
}

// ---------------------------------------------------------------------------
// ViewerDelete
// ---------------------------------------------------------------------------

bool LiveStreamServiceImpl::ViewerDelete(const std::string& channelId, const std::string& algCode) {
    std::shared_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopping_.load(std::memory_order_acquire)) {
        return true;
    }

    cosmo::StreamViewerPtr viewer_to_stop;
    std::string channel_lease_to_release;
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = FindViewer(channelId, algCode);

        if (it != viewers_.end()) {
            (*it)->DelViewerNum();
            LOG_INFO("viewer release requested: stream={}/{} viewers_remaining={}", channelId, algCode,
                     (*it)->GetViewerNum());
            if ((*it)->GetViewerNum() <= 0) {
                viewer_to_stop = *it;
                viewers_.erase(it);
            }
        } else {
            const auto starting_it = starting_viewers_.find(BuildViewerKey(channelId, algCode));
            if (starting_it != starting_viewers_.end()) {
                auto& gate = starting_it->second;
                if (gate->participants > 0) {
                    gate->participants -= 1;
                }
                if (gate->participants == 0) {
                    gate->cancelled = true;
                    viewer_to_stop  = gate->viewer;
                    if (gate->channel_lease_acquired) {
                        channel_lease_to_release     = gate->channel_id;
                        gate->channel_lease_acquired = false;
                    }
                    LOG_INFO("viewer startup cancel requested: stream={}/{} waiters=0", channelId, algCode);
                } else {
                    LOG_INFO("viewer startup participant released: stream={}/{} waiters_remaining={}",
                             channelId, algCode, gate->participants);
                }
            } else {
                LOG_INFO("viewer release idempotent: stream={}/{} state=absent", channelId, algCode);
            }
        }
    }
    if (viewer_to_stop) {
        viewer_to_stop->Stop();
    }
    if (!channel_lease_to_release.empty()) {
        service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>().ReleasePreviewChannel(
            channel_lease_to_release);
    }
    return true;
}

// ---------------------------------------------------------------------------
// ViewerHeartBeat
// ---------------------------------------------------------------------------

cosmo::util::ErrorEnum LiveStreamServiceImpl::ViewerHeartBeat(const std::string& channelId,
                                                              const std::string& algCode) {
    std::shared_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopping_.load(std::memory_order_acquire)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    auto channel_inst =
        service::ServiceRegistry::Instance().Get<service::ICameraChannelQuery>().GetChannelInst(channelId);
    if (!channel_inst) {
        return cosmo::util::ErrorEnum::CameraNotExist;
    }
    std::shared_lock<std::shared_mutex> lock(mtx_);
    LOG_DEBUG("alive channel size {} {}", viewers_.size(), channelId);
    auto it = FindViewer(channelId, algCode);
    if (it != viewers_.end()) {
        if (!(*it)->IsPublishReady()) {
            LOG_WARN("viewer heartbeat rejected: stream={}/{} publisher=failed detail={}", channelId, algCode,
                     (*it)->LastPublishError());
            return cosmo::util::ErrorEnum::LiveStreamPublishFailed;
        }
        (*it)->HeartBeat();
        return cosmo::util::ErrorEnum::Success;
    }

    // A successful keepalive for a missing viewer leaves the browser believing
    // that a stream removed by the watchdog (or a service restart) is still
    // healthy. Return the source error when one exists, otherwise a normal
    // no-data error so the client can recreate it. An existing publisher is
    // deliberately checked before source state: a short RTSP outage must not
    // tear down a healthy viewer that can resume as soon as demux reconnects.
    const cosmo::util::ErrorEnum channelState = channel_inst->GetUrlStatus();
    if (cosmo::util::ErrorEnum::Success != channelState) {
        LOG_WARN("Channel Stream State {}", channelState);
        return channelState;
    }
    return cosmo::util::ErrorEnum::DemuxNoData;
}

// ---------------------------------------------------------------------------
// SetViewCounts
// ---------------------------------------------------------------------------

void LiveStreamServiceImpl::SetViewCounts(int view_num) {
    std::shared_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopping_.load(std::memory_order_acquire)) {
        return;
    }
    view_counts_.store(view_num);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

int LiveStreamServiceImpl::ViewerEncoderCountLocked() const {
    // Caller must hold mtx_ (exclusive or shared)
    const auto ready_count    = std::count_if(viewers_.begin(), viewers_.end(),
                                              [](const auto& viewer) { return viewer->HaveEncoder(); });
    const auto starting_count = std::count_if(
        starting_viewers_.begin(), starting_viewers_.end(),
        [](const auto& item) { return item.second->requires_encoder && !item.second->finished; });
    return static_cast<int>(ready_count + starting_count);
}

void LiveStreamServiceImpl::CheckAliveTasks() {
    std::vector<cosmo::StreamViewerPtr> viewers_to_stop;
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = viewers_.begin();
        while (it != viewers_.end()) {
            const bool publisher_failed = !(*it)->IsPublishReady();
            const bool heartbeat_failed = (*it)->HeartBeatCheck();
            if (publisher_failed || heartbeat_failed) {
                LOG_INFO("viewer watchdog release: stream={}/{} viewers={} reason={}", (*it)->GetChannelId(),
                         (*it)->GetAlgId(), (*it)->GetViewerNum(),
                         publisher_failed ? "publisher-failed" : "heartbeat-timeout");
                viewers_to_stop.push_back(*it);
                it = viewers_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& viewer : viewers_to_stop) {
        StopViewerAndReleasePreview(viewer);
    }
}

std::vector<cosmo::StreamViewerPtr>::iterator LiveStreamServiceImpl::FindViewer(const std::string& channelId,
                                                                                const std::string& algCode) {
    return std::find_if(viewers_.begin(), viewers_.end(), [&](const cosmo::StreamViewerPtr& viewer) {
        return viewer->GetChannelId() == channelId && viewer->GetAlgId() == algCode;
    });
}

}  // namespace cosmo::service
