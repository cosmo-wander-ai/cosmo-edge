// AlgChannel — channel lifecycle, parameter management and task binding.

#include "flow/channel/AlgChannel.h"

#include "flow/channel/ChannelStartTransaction.h"
#include "flow/channel/ChannelStopTransaction.h"
#include "flow/channel/NativeRawPreview.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/ProcessShutdown.h"
#include "util/SafeParse.h"
#include "util/dto/CameraMsgTypes.h"

static constexpr const char* kTag = "ALGCHANNEL ";
namespace cosmo {

AlgChannel::~AlgChannel() {
    Quit();
    LOG_INFO("Channel:{} Delete", channel);
}

AlgChannel::AlgChannel(const std::string& channel_id, const std::string& init_task_id, ActionNode& action,
                       const std::string& url)
    : AlgActionBase(AlgActionType::AlgActionChannelData, action, channel_id, ""),
      url_(url),
      demuxer_(channel_id, url),
      decoder_(*this, channel_id) {
    AddTask(init_task_id);
    LOG_INFO("Channel:{}/{} Init", channel, init_task_id);
}

bool AlgChannel::RegistTaskQueue(AlgTaskUnit& param) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    std::shared_lock<std::shared_mutex> lock(mtx_);
    if (!native_face_reservation_.AllowsTask(param.task_id))
        return false;
#endif
    if (AlgDataType::ChannelDataOrig == param.dataType) {
        return demuxer_.RegistProcQueue(param);
    }

    return decoder_.RegistProcQueue(param);
}

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
bool AlgChannel::ReserveNativeFace(const std::shared_ptr<const void>& token,
                                   const std::string& expected_task_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    const bool joining =
        is_started_ && native_face_reservation_.CanJoin(expected_task_id, decoder_.NativeSourceIs1080p());
    const bool stopped = !is_started_ && base_worker_stopped_.load() && demux_worker_stopped_.load() &&
                         decoder_worker_stopped_.load() && decoder_.NativeFaceStoppedCleanly();
    return !lifecycle_transition_ && (joining || stopped) && active_capture_requests_ == 0 &&
           !demuxer_.HasUnisolatedPacketViewers() && !decoder_.HasNativeIncompatibleConsumers() &&
           native_face_reservation_.Reserve(is_started_, tasks_, token, expected_task_id, camera_owned_task_);
}

bool AlgChannel::IsReservedCameraMetadataTask(const std::string& task_id) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return camera_owned_task_ == task_id && native_face_reservation_.IsCameraMetadataTask(task_id);
}

bool AlgChannel::MarkCameraOwnedTask(const std::string& camera_task_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (is_started_ || lifecycle_transition_ || native_face_reservation_.Active() ||
        !camera_owned_task_.empty() || camera_task_id != channel + "-ChannelTask" ||
        std::find(tasks_.begin(), tasks_.end(), camera_task_id) == tasks_.end())
        return false;
    camera_owned_task_ = camera_task_id;
    return true;
}

bool AlgChannel::ReleaseNativeFace(const std::shared_ptr<const void>& token) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    // Keep the reservation if the channel is still running. Stop() alone does
    // not join demux/decode; Quit() must complete first.
    if (lifecycle_transition_)
        return false;
    if (native_face_reservation_.HasOther(token))
        return native_face_reservation_.Release(is_started_, token);
    return base_worker_stopped_.load() && demux_worker_stopped_.load() && decoder_worker_stopped_.load() &&
           decoder_.NativeFaceStoppedCleanly() && native_face_reservation_.Release(is_started_, token);
}
#endif

bool AlgChannel::RemoveTaskQueue(AlgTaskUnit& param) {
    if (AlgDataType::ChannelDataOrig == param.dataType) {
        return demuxer_.RemoveProcQueue(param);
    }

    return decoder_.RemoveProcQueue(param);
}

int AlgChannel::ForceRemoveByTaskId(const std::string& target_task_id) {
    // AlgChannel routes queues to either demuxer or decoder distributors,
    // so we must clean both to catch any stale entries.
    int count = 0;
    count += demuxer_.ForceRemoveByTaskId(target_task_id);
    count += decoder_.ForceRemoveByTaskId(target_task_id);
    return count;
}

void AlgChannel::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status, unsigned int duration_sec) {
    // Demux has no queue of its own.
    demuxer_.QueueStatus(que_status, duration_sec);
    decoder_.QueueStatus(que_status, duration_sec);
}

void AlgChannel::ActionInfo(std::vector<ActionRuntimeInfo>& action_info) {
    demuxer_.ActionInfo(action_info);
    decoder_.ActionInfo(action_info);
}

bool AlgChannel::AddViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue) {
    std::unique_lock<std::shared_mutex> lock(mtx_);
    if (!async_packet_queue)
        return false;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_reservation_.Active()) {
        if (demuxer_.GetVideoCodecType() != media::VideoCodecType::kH264 ||
            !native_raw_preview::IsBaselineConfig(demuxer_.GetCodecExtradata()))
            return false;
        async_packet_queue->SetMaxSize(native_raw_preview::kQueuePackets);
        demuxer_.AddIsolatedViewerPacketQueue(async_packet_queue);
        return true;
    }
#endif
    demuxer_.AddViewerPacketQueue(async_packet_queue);
    return true;
}

void AlgChannel::RemoveViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue) {
    demuxer_.RemoveViewerPacketQueue(async_packet_queue);
}

bool AlgChannel::AddViewerFrameQueue(const std::string& alg_id, AsyncQueue<VideoFramePtr>& async_frame_queue,
                                     std::function<bool()> prepare_frame) {
    std::unique_lock<std::shared_mutex> lock(mtx_);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    // Native preview is admitted only after the inference task has started,
    // and only with the bounded, filtered host-copy path.
    if (native_face_reservation_.Active() && (alg_id.empty() || !prepare_frame))
        return false;
#endif
    decoder_.AddViewerFrameQueue(alg_id, async_frame_queue, std::move(prepare_frame));
    return true;
}

void AlgChannel::RemoveViewerFrameQueue(const std::string& alg_id) {
    decoder_.RemoveViewerFrameQueue(alg_id);
}

bool AlgChannel::SetUrl(const std::string& url) {
    LOG_INFO("Channel {} Set URL.", channel);
    return demuxer_.SetUrl(url);
}

bool AlgChannel::SetVideoRepeatCount(int repeat_count) {
    LOG_INFO("Channel {} Set Video repeatCount:{}.", channel, repeat_count);
    return demuxer_.SetVideoRepeatCount(repeat_count);
}

bool AlgChannel::SetVideoFps(float fps) {
    LOG_INFO("Channel {} Set Video fps:{}.", channel, fps);
    return demuxer_.SetVideoFps(fps);
}

bool AlgChannel::SetPollChannel(const std::string& channel_id) {
    LOG_INFO("Channel {} Set Poll Channel:{}.", channel, channel_id);
    return demuxer_.SetPollChannel(channel_id);
}

std::string AlgChannel::GetUrl() const {
    return demuxer_.GetUrl();
}

std::vector<uint8_t> AlgChannel::GetCodecExtradata() const {
    return demuxer_.GetCodecExtradata();
}

media::VideoCodecType AlgChannel::GetCodecType() const {
    return demuxer_.GetVideoCodecType();
}

util::ErrorEnum AlgChannel::GetUrlStatus() const {
    return demuxer_.GetStatus();
}

bool AlgChannel::GetFrameInfo(bool& is_live, int64_t& index, int64_t& pts, int64_t& frame_size,
                              std::string& stream_url) {
    auto frame = demuxer_.GetLastFrame();
    if (!frame) {
        return false;
    }
    index      = frame->GetSequence();
    pts        = frame->timestamp;
    frame_size = frame->GetSize();
    is_live    = demuxer_.IsLiveStream();
    stream_url = demuxer_.GetUrl();

    LOG_INFO("bLine:{} index:{} frameSize:{} pts:{} ", is_live, index, frame_size, pts);

    return true;
}

bool AlgChannel::GetAttr(MsgCameraAttr& attr) {
    return demuxer_.GetAttr(attr);
}

bool AlgChannel::GetPreviewAttr(MsgCameraAttr& attr) {
    return demuxer_.GetPreviewAttr(attr);
}

VideoFramePtr AlgChannel::CaptureImage(int timeout_ms) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    const bool native_capture = decoder_.HasE24NativeCapture();
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        if (lifecycle_transition_ || (native_face_reservation_.Active() && !native_capture))
            return nullptr;
        ++active_capture_requests_;
    }
    try {
        auto result = decoder_.CaptureImage(timeout_ms);
        std::lock_guard<std::shared_mutex> lock(mtx_);
        --active_capture_requests_;
        return result;
    } catch (...) {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        --active_capture_requests_;
        throw;
    }
#else
    return decoder_.CaptureImage(timeout_ms);
#endif
}

// Modify parameters incrementally on existing values.
bool AlgChannel::ModifyParam(const std::string& /*channel_id*/, const std::string& /*param_task_id*/,
                             std::vector<MsgDynamicKeyValue>& params) {
    int failed_count = 0;
    for (const auto& param : params) {
        if (key::CHANNEL_URL == param.key.ToString()) {
            if (param.value.ToString() != GetUrl()) {
                LOG_INFO("ModifyParam Channel {} URL changed.", channel);
                failed_count += (false == SetUrl(param.value.ToString()));
            }
        } else if (key::CHANNEL_SOURCE_REPEAT == param.key.ToString()) {
            failed_count += (false == SetVideoRepeatCount(util::ParseInt(param.value.ToString())));
        } else if (key::CHANNEL_SOURCE_FPS == param.key.ToString()) {
            failed_count += (false == SetVideoFps(util::ParseFloat(param.value.ToString())));
        } else if (key::alarm::POLLING_VIDEO_CHANNEL_ID == param.key.ToString()) {
            sub_channel_id_ = param.value;
            SetPollChannel(sub_channel_id_);
        }
    }

    return (0 == failed_count);
}

bool AlgChannel::SetParam(const std::string& channel_id, const std::string& param_task_id,
                          std::vector<MsgDynamicKeyValue>& params) {
    return ModifyParam(channel_id, param_task_id, params);
}

bool AlgChannel::IsDataActive() const {
    return demuxer_.IsDataActive();
}

bool AlgChannel::RecordMp4(RecordParam& record_param) {
    return demuxer_.RecordMp4(record_param);
}

// Start channel: launch demuxer and decoder.
bool AlgChannel::Start() {
    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mtx_, std::try_to_lock);
    if (!lifecycle_lock.owns_lock())
        return false;
    bool was_started = false;
    bool strict      = false;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        if (util::ProcessShutdown::Requested() || lifecycle_transition_)
            return false;
        if (!is_started_ && (!base_worker_stopped_.load() || !demux_worker_stopped_.load() ||
                             !decoder_worker_stopped_.load()))
            return false;
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (!native_face_reservation_.Exclusive(tasks_))
            return false;
        strict = native_face_reservation_.Active();
#endif
        was_started           = is_started_;
        lifecycle_transition_ = true;
    }
    bool success = false;
    try {
        ChannelStartOps ops;
        ops.configure = [&]() {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
            return demuxer_.SetNativeFaceWitnessBeforeStart(strict) &&
                   decoder_.SetNativeFaceRequiredBeforeStart(strict);
#else
            return true;
#endif
        };
        ops.start_base = [&]() {
            base_worker_stopped_ = false;
            return AlgActionBase::Start();
        };
        ops.register_queue = [&]() {
            regist_task_.channel_id = channel;
            regist_task_.task_id    = decoder_.GetName();
            regist_task_.fps        = decoder_.GetRequiredFps();
            regist_task_.que        = decoder_.GetQueue();
            return demuxer_.RegistProcQueue(regist_task_);
        };
        ops.start_decoder = [&]() {
            decoder_worker_stopped_ = false;
            return decoder_.Start();
        };
        ops.start_demux = [&]() {
            demux_worker_stopped_ = false;
            return demuxer_.Start();
        };
        ops.resume_existing  = [&]() { return strict || demuxer_.SetForStartTask(); };
        ops.stop_demux       = [&]() { demux_worker_stopped_ = demuxer_.Stop(); };
        ops.stop_decoder     = [&]() { decoder_worker_stopped_ = decoder_.Stop(); };
        ops.unregister_queue = [&]() { (void)demuxer_.RemoveProcQueue(regist_task_); };
        ops.stop_base        = [&]() {
            AlgActionBase::Stop();
            stop();
            base_worker_stopped_ = !joinable();
        };
        success = RunChannelStartTransaction(was_started, ops);
    } catch (...) {
        success = false;
    }
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        if (success)
            is_started_ = true;
        lifecycle_transition_ = false;
    }
    return success;
}

bool AlgChannel::IsStoppedForIdleCleanup() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    const bool stopped = !is_started_ && !lifecycle_transition_ && base_worker_stopped_.load() &&
                         demux_worker_stopped_.load() && decoder_worker_stopped_.load();
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    return stopped && decoder_.NativeFaceStoppedCleanly();
#else
    return stopped;
#endif
}

// Quit channel: stop demuxer and decoder completely.
void AlgChannel::Quit() {
    const auto may_stop = [&]() { return GetActiveTaskCount() <= 0; };
    const bool needs_cleanup =
        !base_worker_stopped_.load() || !demux_worker_stopped_.load() || !decoder_worker_stopped_.load();
    const auto stop_workers = [&]() {
        (void)demuxer_.RemoveProcQueue(regist_task_);
        const bool demux_stopped   = demuxer_.Stop();
        demux_worker_stopped_      = demux_stopped;
        const bool decoder_stopped = decoder_.Stop();
        decoder_worker_stopped_    = decoder_stopped;
        if (!base_worker_stopped_.load()) {
            AlgActionBase::Stop();
            stop();
            base_worker_stopped_ = !joinable();
        }
        return demux_stopped && decoder_stopped && base_worker_stopped_.load();
    };
    (void)RunChannelStopTransaction(lifecycle_mtx_, mtx_, is_started_, lifecycle_transition_, may_stop,
                                    stop_workers, needs_cleanup);
}
// Stop action base consumer thread (task-level stop).
void AlgChannel::Stop() {
    // Stop AlgActionBase consumer thread (if active).
    AlgActionBase::Stop();
    stop();
    base_worker_stopped_ = !joinable();
    std::shared_lock<std::shared_mutex> lock(mtx_);
    if (is_started_) {
        LOG_INFO("Channel:{} Stop", channel);
    }
}

bool AlgChannel::AddTask(const std::string& new_task_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const std::string& task) { return task == new_task_id; });

    if (it != tasks_.end()) {
        return true;  // Channel already exists for this task.
    }
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_face_reservation_.Active() && !native_face_reservation_.CanAttachTask(new_task_id))
        return false;
#endif
    if (lifecycle_transition_)
        return false;
    tasks_.push_back(new_task_id);
    LOG_INFO("Channel:{} Add Task {}", channel, new_task_id);
    return true;
}

bool AlgChannel::RmvTask(const std::string& target_task_id, bool& empty) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    empty   = false;
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const std::string& task) { return task == target_task_id; });

    if (it != tasks_.end()) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_face_reservation_.Active() && !native_face_reservation_.CanDetachTask(target_task_id))
            return false;
        if (*it == camera_owned_task_)
            camera_owned_task_.clear();
#endif
        tasks_.erase(it);

        LOG_INFO("Channel:{} Rmv Task {}", channel, target_task_id);
        empty = tasks_.empty();
        return true;
    }
    return false;
}

std::vector<std::string> AlgChannel::GetTasks() const {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    return tasks_;
}

}  // namespace cosmo
