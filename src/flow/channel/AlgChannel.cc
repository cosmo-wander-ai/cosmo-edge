// AlgChannel — channel lifecycle, parameter management and task binding.

#include "flow/channel/AlgChannel.h"

#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/dto/CameraMsgTypes.h"

static constexpr const char* kTag = "ALGCHANNEL ";
namespace cosmo {

AlgChannel::~AlgChannel() {
    Quit();
    LOG_INFO("Channel:{} Url:{} Delete", channel, url_);
}

AlgChannel::AlgChannel(const std::string& channel_id, const std::string& init_task_id, ActionNode& action,
                       const std::string& url)
    : AlgActionBase(AlgActionType::AlgActionChannelData, action, channel_id, ""),
      url_(url),
      demuxer_(channel_id, url),
      decoder_(*this, channel_id) {
    AddTask(init_task_id);
    LOG_INFO("Channel:{}/{} Url:{} Init", channel, init_task_id, url_);
}

bool AlgChannel::RegistTaskQueue(AlgTaskUnit& param) {
    if (AlgDataType::ChannelDataOrig == param.dataType) {
        return demuxer_.RegistProcQueue(param);
    }

    return decoder_.RegistProcQueue(param);
}

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

void AlgChannel::AddViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue) {
    demuxer_.AddViewerPacketQueue(async_packet_queue);
}

void AlgChannel::RemoveViewerPacketQueue(std::shared_ptr<AsyncQueue<VideoPacketPtr>> async_packet_queue) {
    demuxer_.RemoveViewerPacketQueue(async_packet_queue);
}

void AlgChannel::AddViewerFrameQueue(const std::string& alg_id,
                                     AsyncQueue<VideoFramePtr>& async_frame_queue) {
    decoder_.AddViewerFrameQueue(alg_id, async_frame_queue);
}

void AlgChannel::RemoveViewerFrameQueue(const std::string& alg_id) {
    decoder_.RemoveViewerFrameQueue(alg_id);
}

bool AlgChannel::SetUrl(const std::string& url) {
    LOG_INFO("Channel {} Set Url:{}.", channel, url);
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

VideoFramePtr AlgChannel::CaptureImage(int timeout_ms) {
    return decoder_.CaptureImage(timeout_ms);
}

// Modify parameters incrementally on existing values.
bool AlgChannel::ModifyParam(const std::string& /*channel_id*/, const std::string& /*param_task_id*/,
                             std::vector<MsgDynamicKeyValue>& params) {
    int failed_count = 0;
    for (const auto& param : params) {
        if (key::CHANNEL_URL == param.key.ToString()) {
            if (param.value.ToString() != GetUrl()) {
                LOG_INFO(
                    "ModifyParam "
                    "Channel {} Url Change From :{} To {}.",
                    channel, GetUrl(), param.value);
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
void AlgChannel::Start() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    // AlgChannel inherits AlgActionBase, but this Start() only manages demux/decode.
    // If external code still writes to AlgChannel::GetQueue() (e.g. queue named "-flow-channel--1"),
    // the base-class thread must be started to consume data_queue, otherwise queue fills up with Proc:0.
    bool was_started = is_started_;
    AlgActionBase::Start();

    if (was_started) {
        LOG_INFO("Channel:{} Url:{} Alread Started", channel, url_);
        // Restart VoD stream from the beginning.
        demuxer_.SetForStartTask();
        return;
    }
    is_started_ = true;
    LOG_INFO("Channel:{} Url:{} Start", channel, url_);
    regist_task_.channel_id = channel;
    regist_task_.task_id    = decoder_.GetName();
    regist_task_.fps        = decoder_.GetRequiredFps();
    regist_task_.que        = decoder_.GetQueue();
    demuxer_.RegistProcQueue(regist_task_);
    // Start decoder first (resume queue), then start demuxer;
    // otherwise short video files may finish before queue Resume, dropping all frames.
    decoder_.Start();
    demuxer_.Start();
}

// Quit channel: stop demuxer and decoder completely.
void AlgChannel::Quit() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    if (is_started_) {
        LOG_INFO("Channel:{} Url:{} Stop", channel, url_);
        is_started_ = false;
        demuxer_.RemoveProcQueue(regist_task_);
        decoder_.Stop();
        demuxer_.Stop();
    }
}
// Stop action base consumer thread (task-level stop).
void AlgChannel::Stop() {
    // Stop AlgActionBase consumer thread (if active).
    AlgActionBase::Stop();
    if (is_started_) {
        LOG_INFO("Channel:{} Url:{} Stop", channel, url_);
    }
}

void AlgChannel::AddTask(const std::string& new_task_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const std::string& task) { return task == new_task_id; });

    if (it != tasks_.end()) {
        return;  // Channel already exists for this task.
    }
    tasks_.push_back(new_task_id);
    LOG_INFO("Channel:{} Add Task {}", channel, new_task_id);
}

bool AlgChannel::RmvTask(const std::string& target_task_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const std::string& task) { return task == target_task_id; });

    if (it != tasks_.end()) {
        tasks_.erase(it);

        LOG_INFO("Channel:{} Rmv Task {}", channel, target_task_id);
        return tasks_.empty();  // True when no tasks remain.
    }
    return false;
}

std::vector<std::string> AlgChannel::GetTasks() const {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    return tasks_;
}

}  // namespace cosmo
