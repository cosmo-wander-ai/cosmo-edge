// AlgChannelMng — channel instance management and task-channel binding.

#include "flow/channel/AlgChannelMng.h"

#include <algorithm>
#include <iterator>

#include "util/Log.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {
AlgChannelMng::AlgChannelMng() {
    LOG_INFO("{}", "ChannelMng Init");
}

AlgChannelMng::~AlgChannelMng() {
    LOG_INFO("{}", "ChannelMng Delete");
}

AlgChannelPtr AlgChannelMng::GetInst(const std::string& channel_id, const std::string& task_id,
                                     ActionNode& action) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    // Search under write lock to prevent TOCTOU race leading to duplicate creation.
    auto it = std::find_if(channels_.begin(), channels_.end(),
                           [&](const AlgChannelPtr& channel) { return channel->GetChannel() == channel_id; });
    if (it != channels_.end()) {
        (*it)->AddTask(task_id);
        return *it;
    }
    auto channel = std::make_shared<AlgChannel>(channel_id, task_id, action);
    channels_.push_back(channel);
    return channel;
}

// Get an existing channel instance (read-only lookup).
AlgChannelPtr AlgChannelMng::GetChannelInst(const std::string& channel_id) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(channels_.begin(), channels_.end(),
                           [&](const AlgChannelPtr channel) { return channel->GetChannel() == channel_id; });

    if (it != channels_.end()) {
        return *it;
    }
    return nullptr;
}

std::vector<std::string> AlgChannelMng::GetChannelTasks(const std::string& channel_id) {
    auto inst = GetChannelInst(channel_id);
    if (!inst) {
        return {};
    }

    return inst->GetTasks();
}

bool AlgChannelMng::DeleteInst(AlgChannelPtr inst, const std::string& task_id) {
    if (!inst) {
        LOG_WARN("{}", "Delete Channel But Inst Is Empty");
        return false;
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(channels_.begin(), channels_.end(), [&](const AlgChannelPtr channel) {
        return channel->GetChannel() == inst->GetChannel();
    });
    if (it != channels_.end()) {
        if ((*it)->RmvTask(task_id)) {
            LOG_INFO("Delete Channel:{}", inst->GetChannel());
            channels_.erase(it);
        }
        return true;
    }

    LOG_WARN("Delete Channel {} But Cant Find It", inst->GetChannel());
    return false;
}

void AlgChannelMng::QueueStatus(std::vector<AlgActionDataQueueStatus>& que_status,
                                unsigned int duration_sec) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto& inst : channels_) {
        inst->QueueStatus(que_status, duration_sec);
    }
}

void AlgChannelMng::ActionInfo(std::vector<ActionRuntimeInfo>& action_info) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto& inst : channels_) {
        inst->ActionInfo(action_info);
    }
}

bool AlgChannelMng::SetUrl(const std::string& channel_id, const std::string& url) {
    auto channel = GetChannelInst(channel_id);
    if (!channel) {
        LOG_WARN("Channel {} Set Url, But's Channel Not Exist.", channel_id);
        return false;
    }
    return channel->SetUrl(url);
}

bool AlgChannelMng::SetVideoRepeatCount(const std::string& channel_id, int repeat_count) {
    auto channel = GetChannelInst(channel_id);
    if (!channel) {
        LOG_WARN("Channel {} Set Url, But's Channel Not Exist.", channel_id);
        return false;
    }
    return channel->SetVideoRepeatCount(repeat_count);
}

bool AlgChannelMng::SetVideoFps(const std::string& channel_id, float fps) {
    auto channel = GetChannelInst(channel_id);
    if (!channel) {
        LOG_WARN("Channel {} Set Url, But's Channel Not Exist.", channel_id);
        return false;
    }
    return channel->SetVideoFps(fps);
}

bool AlgChannelMng::GetFrameInfo(const std::string& channel_id, bool& is_live, int64_t& index, int64_t& pts,
                                 int64_t& frame_size, std::string& stream_url) {
    auto channel = GetChannelInst(channel_id);
    if (!channel) {
        return false;
    }
    return channel->GetFrameInfo(is_live, index, pts, frame_size, stream_url);
}

void AlgChannelMng::AddTaskChannel(MsgTaskCreateRecv task_create_recv) {
    auto channel = GetChannelInst(task_create_recv.videoChannelId);
    if (!channel) {
        LOG_WARN("Channel {} Add Task {}, But's Channel Not Exist.", task_create_recv.videoChannelId,
                 task_create_recv.taskId);
        return;
    }
    MsgCameraAttr video_attr;
    if (!channel->GetAttr(video_attr)) {
        LOG_WARN("Failed to get attr for channel {}", task_create_recv.videoChannelId);
    }
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = camera_info_map_.find(task_create_recv.videoChannelId);
    if (it == camera_info_map_.end()) {
        // Not found; create a new entry.
        MsgCameraInfo channel_data;
        channel_data.videoChannelId = task_create_recv.videoChannelId;
        channel_data.channelName    = task_create_recv.videoChannelName;
        channel_data.url            = task_create_recv.streamUrl;

        channel_data.codec  = video_attr.codec;
        channel_data.width  = video_attr.width;
        channel_data.height = video_attr.height;
        channel_data.fps    = video_attr.fps;

        MsgCameraTask task;
        task.algorithmId   = task_create_recv.algorithmId;
        task.algorithmName = task_create_recv.algorithmName;
        channel_data.taskList.push_back(task);

        // Insert the new entry.
        camera_info_map_[task_create_recv.videoChannelId] = channel_data;
    } else {
        // Already exists; append task directly.
        auto& channel_data = it->second;
        MsgCameraTask task;
        task.algorithmId   = task_create_recv.algorithmId;
        task.algorithmName = task_create_recv.algorithmName;
        channel_data.taskList.push_back(task);
    }
}

void AlgChannelMng::DeleteTaskChannel(MsgTaskCancleRecv task_cancel_recv, const std::string& channel_id,
                                      const std::string& algorithm_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = camera_info_map_.find(channel_id);
    if (it == camera_info_map_.end()) {
        LOG_WARN("Channel {} Delete Task {}, But's Channel Not Exist.", channel_id, task_cancel_recv.taskId);
        return;
    }
    auto& channel_data = it->second;
    auto task_it       = std::find_if(channel_data.taskList.begin(), channel_data.taskList.end(),
                                      [&](const MsgCameraTask& task) { return task.algorithmId == algorithm_id; });
    if (task_it != channel_data.taskList.end()) {
        channel_data.taskList.erase(task_it);
    }
    if (channel_data.taskList.empty()) {
        camera_info_map_.erase(it);
    }
}

void AlgChannelMng::GetCameraInfo(std::vector<MsgCameraInfo>& camera_infos) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    std::transform(camera_info_map_.begin(), camera_info_map_.end(), std::back_inserter(camera_infos),
                   [](const auto& pair) { return pair.second; });
}
}  // namespace cosmo
