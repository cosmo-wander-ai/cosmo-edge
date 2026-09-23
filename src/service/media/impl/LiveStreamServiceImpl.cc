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

    cosmo::util::ErrorEnum PreviewTaskState(const std::string& channel_id, const std::string& algorithm_id) {
        if (algorithm_id.empty()) {
            return cosmo::util::ErrorEnum::Success;
        }
        const auto tasks = ServiceRegistry::Instance().Get<ICameraTaskConfig>().GetTasks(channel_id);
        const auto task  = std::find_if(tasks.begin(), tasks.end(), [&](const auto& entry) {
            return entry.algorithmCode == algorithm_id;
        });
        if (task == tasks.end()) {
            return cosmo::util::ErrorEnum::TaskNotExist;
        }
        return task->enable ? cosmo::util::ErrorEnum::Success : cosmo::util::ErrorEnum::ActionStop;
    }

    std::string BuildStreamName(const std::string& channelId, const std::string& algCode) {
        return algCode.empty() ? channelId : COSMO_FORMAT("{}_{}", channelId, algCode);
    }

    void PopulateStreamInfo(LiveStream::LiveStreamInfo& info, const std::string& channelId,
                            const std::string& algCode, const std::string& previewSessionId) {
        info.previewSessionId        = previewSessionId;
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
        viewer_session_ids_.clear();
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
    return ViewerCreate(channelId, algCode, {}, streamInfo);
}

cosmo::util::ErrorEnum LiveStreamServiceImpl::ViewerCreate(const std::string& channelId,
                                                           const std::string& algCode,
                                                           const std::string& previewSessionId,
                                                           LiveStream::LiveStreamInfo& streamInfo) {
    std::shared_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopping_.load(std::memory_order_acquire)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    if (previewSessionId.size() > 128) {
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    const auto request_started_at = std::chrono::steady_clock::now();
    auto channel_inst = ServiceRegistry::Instance().Get<ICameraChannelQuery>().GetChannelInst(channelId);
    if (!channel_inst) {
        return cosmo::util::ErrorEnum::CameraNotExist;
    }
    auto task_state = PreviewTaskState(channelId, algCode);
    if (task_state != cosmo::util::ErrorEnum::Success) {
        return task_state;
    }
    auto& camera_task_config = ServiceRegistry::Instance().Get<ICameraTaskConfig>();
    const auto ready_timeout = StreamReadyTimeout(algCode);
    PreviewChannelLease channel_lease(camera_task_config, channelId);
    const auto lease_result = channel_lease.Acquire();
    if (lease_result != cosmo::util::ErrorEnum::Success) {
        return lease_result;
    }
    auto channel_state = channel_inst->GetUrlStatus();
    if (!WaitForChannelReady(channel_inst, ready_timeout, channel_state)) {
        return cosmo::util::ErrorEnum::DemuxNoData;
    }
    cosmo::MsgCameraAttr attr;
    if (!channel_inst->GetAttr(attr)) {
        return cosmo::util::ErrorEnum::CameraNotOnline;
    }
    if (attr.channelStatus != cosmo::ChannelStatus::ChannelStatusOnline) {
        return cosmo::util::ErrorEnum::CameraNotOnline;
    }

    const bool passthrough       = algCode.empty() && attr.codec == "H264";
    const auto deadline          = std::chrono::steady_clock::now() + ready_timeout;
    const std::string viewer_key = BuildViewerKey(channelId, algCode);
    std::shared_ptr<ViewerStartGate> gate;
    bool start_owner       = false;
    bool participant_added = false;
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        for (;;) {
            // Keep this key reserved until the old publisher has fully detached
            // its queues. Other keys remain available while Stop() joins workers.
            auto retiring = retiring_viewers_.find(viewer_key);
            if (retiring != retiring_viewers_.end()) {
                auto barrier = retiring->second;
                if (!barrier->cv.wait_until(lock, deadline, [&] { return barrier->finished; })) {
                    return cosmo::util::ErrorEnum::LiveStreamReadyTimeout;
                }
                continue;
            }
            auto ready = FindViewer(channelId, algCode);
            if (ready != viewers_.end()) {
                const auto observed = *ready;
                lock.unlock();
                task_state = PreviewTaskState(channelId, algCode);
                lock.lock();
                ready = FindViewer(channelId, algCode);
                if (ready == viewers_.end() || *ready != observed) {
                    continue;
                }
                if (task_state != cosmo::util::ErrorEnum::Success) {
                    auto retirement = BeginRetirementLocked(viewer_key, observed);
                    lock.unlock();
                    FinishRetirement(viewer_key, retirement);
                    return task_state;
                }
                if ((*ready)->IsPublishReady()) {
                    auto& sessions = viewer_session_ids_[viewer_key];
                    if (previewSessionId.empty()) {
                        (*ready)->UpViewerNum();
                    } else {
                        auto [session, added] =
                            sessions.try_emplace(previewSessionId, std::chrono::steady_clock::now());
                        session->second = std::chrono::steady_clock::now();
                        if (added) {
                            (*ready)->UpViewerNum();
                        }
                    }
                    // A successful acquisition is fresh activity too. Do not
                    // let the previous client's failure count retire this lease
                    // before its first scheduled heartbeat arrives.
                    (*ready)->HeartBeat();
                    PopulateStreamInfo(streamInfo, channelId, algCode, previewSessionId);
                    return cosmo::util::ErrorEnum::Success;
                }
                auto retirement = BeginRetirementLocked(viewer_key, *ready);
                lock.unlock();
                FinishRetirement(viewer_key, retirement);
                lock.lock();
                continue;
            }
            auto starting = starting_viewers_.find(viewer_key);
            if (starting != starting_viewers_.end()) {
                gate = starting->second;
                if (gate->cancelled) {
                    if (!gate->cv.wait_until(lock, deadline, [&] { return gate->finished; })) {
                        return cosmo::util::ErrorEnum::LiveStreamReadyTimeout;
                    }
                    continue;
                }
                participant_added =
                    previewSessionId.empty() || gate->session_ids.insert(previewSessionId).second;
                if (participant_added) {
                    ++gate->participants;
                }
            } else {
                if (!passthrough && ViewerEncoderCountLocked() >= view_counts_.load()) {
                    return cosmo::util::ErrorEnum::EncodeFailed;
                }
                gate               = std::make_shared<ViewerStartGate>();
                gate->participants = 1;
                if (!previewSessionId.empty()) {
                    gate->session_ids.insert(previewSessionId);
                }
                participant_added            = true;
                gate->channel_id             = channelId;
                gate->algorithm_id           = algCode;
                gate->requires_encoder       = !passthrough;
                gate->channel_lease_acquired = true;
                starting_viewers_.emplace(viewer_key, gate);
                channel_lease.Commit();
                start_owner = true;
            }
            break;
        }
    }
    if (!start_owner) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        if (!gate->cv.wait_until(lock, deadline, [&] { return gate->finished; })) {
            if (participant_added && gate->participants > 0 &&
                (previewSessionId.empty() || gate->session_ids.erase(previewSessionId) != 0)) {
                --gate->participants;
                if (gate->participants == 0) {
                    gate->cancelled = true;
                }
            }
            return cosmo::util::ErrorEnum::LiveStreamReadyTimeout;
        }
        const auto result = !previewSessionId.empty() && !gate->session_ids.count(previewSessionId)
                                ? cosmo::util::ErrorEnum::LiveStreamStopped
                                : gate->result;
        lock.unlock();
        if (result == cosmo::util::ErrorEnum::Success) {
            PopulateStreamInfo(streamInfo, channelId, algCode, previewSessionId);
        }
        return result;
    }

    auto finish_failure = [&](cosmo::util::ErrorEnum result, cosmo::StreamViewerPtr viewer) {
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
        LOG_WARN("viewer startup failed: stream={}/{} error={}", channelId, algCode,
                 cosmo::util::ErrorEnumName(result));
    };
    auto remaining = [&] {
        return std::max(std::chrono::milliseconds::zero(),
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now()));
    };
    cosmo::StreamViewerPtr viewer;
    try {
        // Task state can change during channel startup or retirement of an old publisher.
        task_state = PreviewTaskState(channelId, algCode);
        if (task_state != cosmo::util::ErrorEnum::Success) {
            finish_failure(task_state, nullptr);
            return task_state;
        }
        {
            std::unique_lock<std::shared_mutex> lock(mtx_);
            if (gate->cancelled) {
                const auto result = gate->cancel_result;
                lock.unlock();
                finish_failure(result, nullptr);
                return result;
            }
        }
        viewer = std::make_shared<cosmo::StreamViewer>(channel_inst, channelId, algCode);
        {
            std::unique_lock<std::shared_mutex> lock(mtx_);
            gate->viewer = viewer;
            if (gate->cancelled) {
                const auto result = gate->cancel_result;
                lock.unlock();
                finish_failure(result, viewer);
                return result;
            }
        }
        // Give ordinary two-second GOPs time to supply their next IDR before
        // spending an encoder. The publisher deadline still totals five seconds.
        bool ready = viewer->WaitReady(passthrough ? std::min(remaining(), std::chrono::milliseconds(3000))
                                                   : remaining());
        if (!ready && passthrough && viewer->LastPublishError().empty()) {
            bool encoder_available = false;
            {
                std::unique_lock<std::shared_mutex> lock(mtx_);
                if (gate->cancelled) {
                    const auto result = gate->cancel_result;
                    lock.unlock();
                    finish_failure(result, viewer);
                    return result;
                }
                encoder_available = ViewerEncoderCountLocked() < view_counts_.load();
                if (encoder_available) {
                    gate->requires_encoder = true;
                }
            }
            if (!encoder_available) {
                LOG_INFO("raw preview waiting for IDR: channel={} encoder capacity unavailable", channelId);
                // A normal GOP may still deliver its IDR within the original
                // timeout. Lack of encoder capacity must not shorten that wait.
                ready = viewer->WaitReady(remaining());
                if (!ready && viewer->LastPublishError().empty()) {
                    cosmo::util::ErrorEnum result;
                    {
                        std::shared_lock<std::shared_mutex> lock(mtx_);
                        result = gate->cancelled ? gate->cancel_result : cosmo::util::ErrorEnum::EncodeFailed;
                    }
                    finish_failure(result, viewer);
                    return result;
                }
            } else {
                LOG_INFO("raw preview switching to fresh H264 encoder: channel={} remaining_ms={}", channelId,
                         remaining().count());
                // A long GOP cannot be shortened by replaying a stale I frame.
                // Reserve capacity, fully detach the old publisher, and encode
                // a fresh continuous decoded frame within the original budget.
                viewer->Stop();
                {
                    std::unique_lock<std::shared_mutex> lock(mtx_);
                    gate->viewer.reset();
                    if (gate->cancelled || remaining() <= std::chrono::milliseconds::zero()) {
                        const auto result = gate->cancelled ? gate->cancel_result
                                                            : cosmo::util::ErrorEnum::LiveStreamReadyTimeout;
                        lock.unlock();
                        finish_failure(result, viewer);
                        return result;
                    }
                }
                viewer = std::make_shared<cosmo::StreamViewer>(channel_inst, channelId, algCode, true);
                {
                    std::unique_lock<std::shared_mutex> lock(mtx_);
                    gate->viewer = viewer;
                    if (gate->cancelled) {
                        const auto result = gate->cancel_result;
                        lock.unlock();
                        finish_failure(result, viewer);
                        return result;
                    }
                }
                ready = viewer->WaitReady(remaining());
            }
        }
        if (!ready) {
            cosmo::util::ErrorEnum result;
            {
                std::shared_lock<std::shared_mutex> lock(mtx_);
                result = gate->cancelled ? gate->cancel_result
                         : viewer->LastPublishError().empty()
                             ? cosmo::util::ErrorEnum::LiveStreamReadyTimeout
                             : cosmo::util::ErrorEnum::LiveStreamPublishFailed;
            }
            finish_failure(result, viewer);
            return result;
        }
        task_state = PreviewTaskState(channelId, algCode);
        if (task_state != cosmo::util::ErrorEnum::Success) {
            finish_failure(task_state, viewer);
            return task_state;
        }
    } catch (const cosmo::util::ErrorMessage& error) {
        const auto result = static_cast<cosmo::util::ErrorEnum>(error.GetValue().value());
        finish_failure(result, viewer);
        return result;
    } catch (const std::exception& error) {
        LOG_ERRO("viewer startup failed: stream={}/{} detail={}", channelId, algCode, error.what());
        finish_failure(cosmo::util::ErrorEnum::LiveStreamPublishFailed, viewer);
        return cosmo::util::ErrorEnum::LiveStreamPublishFailed;
    }
    bool owner_active = true;
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        if (gate->cancelled || gate->participants == 0) {
            const auto result = gate->cancel_result;
            lock.unlock();
            finish_failure(result, viewer);
            return result;
        }
        for (size_t i = 1; i < gate->participants; ++i) {
            viewer->UpViewerNum();
        }
        viewer->MarkReady(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - request_started_at));
        viewers_.push_back(viewer);
        auto& sessions = viewer_session_ids_[viewer_key];
        const auto now = std::chrono::steady_clock::now();
        for (const auto& id : gate->session_ids) {
            sessions.emplace(id, now);
        }
        owner_active   = previewSessionId.empty() || gate->session_ids.count(previewSessionId) != 0;
        gate->result   = cosmo::util::ErrorEnum::Success;
        gate->finished = true;
        gate->channel_lease_acquired = false;
        gate->viewer.reset();
        starting_viewers_.erase(viewer_key);
        LOG_INFO("viewer startup ready: stream={}/{} viewers={} encoded_raw={}", channelId, algCode,
                 viewer->GetViewerNum(), passthrough && gate->requires_encoder);
    }
    gate->cv.notify_all();
    if (!owner_active) {
        return cosmo::util::ErrorEnum::LiveStreamStopped;
    }
    PopulateStreamInfo(streamInfo, channelId, algCode, previewSessionId);
    return cosmo::util::ErrorEnum::Success;
}

// ---------------------------------------------------------------------------
// ViewerDelete
// ---------------------------------------------------------------------------

bool LiveStreamServiceImpl::ViewerDelete(const std::string& channelId, const std::string& algCode) {
    return ViewerDelete(channelId, algCode, {});
}

bool LiveStreamServiceImpl::ViewerDelete(const std::string& channelId, const std::string& algCode,
                                         const std::string& previewSessionId) {
    std::shared_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopping_.load(std::memory_order_acquire)) {
        return true;
    }
    const auto key = BuildViewerKey(channelId, algCode);
    std::shared_ptr<ViewerRetirement> retirement;
    cosmo::StreamViewerPtr starting_to_stop;
    bool release_starting_lease = false;
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = FindViewer(channelId, algCode);
        if (it != viewers_.end()) {
            auto& sessions = viewer_session_ids_[key];
            if (previewSessionId.empty()) {
                // An unscoped legacy Stop cannot consume a modern client's lease.
                if (static_cast<size_t>((*it)->GetViewerNum()) <= sessions.size()) {
                    return true;
                }
            } else if (sessions.erase(previewSessionId) == 0) {
                return true;
            }
            (*it)->DelViewerNum();
            LOG_INFO("viewer release requested: stream={}/{} viewers_remaining={}", channelId, algCode,
                     (*it)->GetViewerNum());
            if ((*it)->GetViewerNum() <= 0) {
                retirement = BeginRetirementLocked(key, *it);
            }
        } else {
            auto starting = starting_viewers_.find(key);
            if (starting != starting_viewers_.end()) {
                auto& gate = starting->second;
                if (previewSessionId.empty()) {
                    if (gate->participants <= gate->session_ids.size()) {
                        return true;
                    }
                } else if (gate->session_ids.erase(previewSessionId) == 0) {
                    return true;
                }
                if (gate->participants > 0) {
                    --gate->participants;
                }
                if (gate->participants == 0) {
                    gate->cancelled              = true;
                    starting_to_stop             = gate->viewer;
                    release_starting_lease       = gate->channel_lease_acquired;
                    gate->channel_lease_acquired = false;
                }
            }
        }
    }
    if (retirement) {
        FinishRetirement(key, retirement);
    }
    if (starting_to_stop) {
        starting_to_stop->Stop();
    }
    if (release_starting_lease) {
        ServiceRegistry::Instance().Get<ICameraTaskConfig>().ReleasePreviewChannel(channelId);
    }
    return true;
}

cosmo::util::ErrorEnum LiveStreamServiceImpl::ViewerHeartBeat(const std::string& channelId,
                                                              const std::string& algCode) {
    return ViewerHeartBeat(channelId, algCode, {});
}

cosmo::util::ErrorEnum LiveStreamServiceImpl::ViewerHeartBeat(const std::string& channelId,
                                                              const std::string& algCode,
                                                              const std::string& previewSessionId) {
    std::shared_lock<std::shared_mutex> lifecycle_lock(lifecycle_mtx_);
    if (stopping_.load(std::memory_order_acquire)) {
        return cosmo::util::ErrorEnum::SysErr;
    }
    const auto key = BuildViewerKey(channelId, algCode);
    cosmo::StreamViewerPtr observed;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        auto it = FindViewer(channelId, algCode);
        if (it != viewers_.end()) {
            observed                  = *it;
            const auto sessions       = viewer_session_ids_.find(key);
            const size_t scoped_count = sessions == viewer_session_ids_.end() ? 0 : sessions->second.size();
            if ((!previewSessionId.empty() &&
                 (sessions == viewer_session_ids_.end() || !sessions->second.count(previewSessionId))) ||
                (previewSessionId.empty() && static_cast<size_t>(observed->GetViewerNum()) <= scoped_count)) {
                return cosmo::util::ErrorEnum::LiveStreamStopped;
            }
        }
    }
    auto channel = ServiceRegistry::Instance().Get<ICameraChannelQuery>().GetChannelInst(channelId);
    const auto task_state =
        channel ? PreviewTaskState(channelId, algCode) : cosmo::util::ErrorEnum::CameraNotExist;
    std::shared_ptr<ViewerRetirement> retirement;
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = FindViewer(channelId, algCode);
        if (!observed || it == viewers_.end() || *it != observed) {
            if (task_state != cosmo::util::ErrorEnum::Success) {
                return task_state;
            }
            return channel->GetUrlStatus() == cosmo::util::ErrorEnum::Success
                       ? cosmo::util::ErrorEnum::DemuxNoData
                       : channel->GetUrlStatus();
        }
        const auto sessions       = viewer_session_ids_.find(key);
        const size_t scoped_count = sessions == viewer_session_ids_.end() ? 0 : sessions->second.size();
        if ((!previewSessionId.empty() &&
             (sessions == viewer_session_ids_.end() || !sessions->second.count(previewSessionId))) ||
            (previewSessionId.empty() && static_cast<size_t>(observed->GetViewerNum()) <= scoped_count)) {
            return cosmo::util::ErrorEnum::LiveStreamStopped;
        }
        if (task_state != cosmo::util::ErrorEnum::Success) {
            retirement = BeginRetirementLocked(key, observed);
        } else if (!observed->IsPublishReady()) {
            return cosmo::util::ErrorEnum::LiveStreamPublishFailed;
        } else {
            if (!previewSessionId.empty()) {
                sessions->second.at(previewSessionId) = std::chrono::steady_clock::now();
            }
            observed->HeartBeat();
            return cosmo::util::ErrorEnum::Success;
        }
    }
    FinishRetirement(key, retirement);
    return task_state;
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

std::shared_ptr<LiveStreamServiceImpl::ViewerRetirement> LiveStreamServiceImpl::BeginRetirementLocked(
    const std::string& key, const cosmo::StreamViewerPtr& viewer) {
    auto retirement              = std::make_shared<ViewerRetirement>();
    retirement->viewer           = viewer;
    retirement->requires_encoder = viewer->HaveEncoder();
    retiring_viewers_[key]       = retirement;
    viewer_session_ids_.erase(key);
    viewers_.erase(std::find(viewers_.begin(), viewers_.end(), viewer));
    return retirement;
}

void LiveStreamServiceImpl::FinishRetirement(const std::string& key,
                                             const std::shared_ptr<ViewerRetirement>& retirement) {
    StopViewerAndReleasePreview(retirement->viewer);
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        retirement->finished = true;
        auto it              = retiring_viewers_.find(key);
        if (it != retiring_viewers_.end() && it->second == retirement) {
            retiring_viewers_.erase(it);
        }
    }
    retirement->cv.notify_all();
}

void LiveStreamServiceImpl::ExpireViewerSessionsLocked(const std::string& key,
                                                       const cosmo::StreamViewerPtr& viewer,
                                                       std::chrono::steady_clock::time_point now) {
    auto sessions = viewer_session_ids_.find(key);
    if (sessions == viewer_session_ids_.end()) {
        return;
    }
    for (auto it = sessions->second.begin(); it != sessions->second.end();) {
        if (now - it->second >= kPreviewSessionTimeout) {
            it = sessions->second.erase(it);
            viewer->DelViewerNum();
        } else {
            ++it;
        }
    }
}

int LiveStreamServiceImpl::ViewerEncoderCountLocked() const {
    const auto ready_count    = std::count_if(viewers_.begin(), viewers_.end(),
                                              [](const auto& viewer) { return viewer->HaveEncoder(); });
    const auto starting_count = std::count_if(
        starting_viewers_.begin(), starting_viewers_.end(),
        [](const auto& item) { return item.second->requires_encoder && !item.second->finished; });
    const auto retiring_count = std::count_if(retiring_viewers_.begin(), retiring_viewers_.end(),
                                              [](const auto& item) { return item.second->requires_encoder; });
    return static_cast<int>(ready_count + starting_count + retiring_count);
}

void LiveStreamServiceImpl::CheckAliveTasks() {
    std::vector<cosmo::StreamViewerPtr> snapshot;
    std::vector<std::pair<std::string, std::shared_ptr<ViewerStartGate>>> starting_snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        snapshot = viewers_;
        for (const auto& item : starting_viewers_) {
            starting_snapshot.push_back(item);
        }
    }
    for (const auto& [key, gate] : starting_snapshot) {
        const auto state = PreviewTaskState(gate->channel_id, gate->algorithm_id);
        if (state == cosmo::util::ErrorEnum::Success) {
            continue;
        }
        cosmo::StreamViewerPtr viewer_to_stop;
        bool release_lease = false;
        {
            std::unique_lock<std::shared_mutex> lock(mtx_);
            auto it = starting_viewers_.find(key);
            if (it == starting_viewers_.end() || it->second != gate || gate->finished || gate->cancelled) {
                continue;
            }
            gate->cancelled              = true;
            gate->cancel_result          = state;
            viewer_to_stop               = gate->viewer;
            release_lease                = gate->channel_lease_acquired;
            gate->channel_lease_acquired = false;
        }
        if (viewer_to_stop) {
            viewer_to_stop->Stop();
        }
        if (release_lease) {
            ServiceRegistry::Instance().Get<ICameraTaskConfig>().ReleasePreviewChannel(gate->channel_id);
        }
    }
    for (const auto& viewer : snapshot) {
        // Camera queries may join a slow switch; never hold the global viewer
        // lock across that call or across publisher destruction.
        const auto state = PreviewTaskState(viewer->GetChannelId(), viewer->GetAlgId());
        const auto key   = BuildViewerKey(viewer->GetChannelId(), viewer->GetAlgId());
        std::shared_ptr<ViewerRetirement> retirement;
        {
            std::unique_lock<std::shared_mutex> lock(mtx_);
            auto it = FindViewer(viewer->GetChannelId(), viewer->GetAlgId());
            if (it == viewers_.end() || *it != viewer) {
                continue;
            }
            ExpireViewerSessionsLocked(key, viewer, std::chrono::steady_clock::now());
            if (state != cosmo::util::ErrorEnum::Success || viewer->GetViewerNum() <= 0 ||
                !viewer->IsPublishReady() || viewer->HeartBeatCheck()) {
                retirement = BeginRetirementLocked(key, viewer);
            }
        }
        if (retirement) {
            FinishRetirement(key, retirement);
        }
    }
}

std::vector<cosmo::StreamViewerPtr>::iterator LiveStreamServiceImpl::FindViewer(const std::string& channelId,
                                                                                const std::string& algCode) {
    return std::find_if(viewers_.begin(), viewers_.end(), [&](const cosmo::StreamViewerPtr& viewer) {
        return viewer->GetChannelId() == channelId && viewer->GetAlgId() == algCode;
    });
}

}  // namespace cosmo::service
