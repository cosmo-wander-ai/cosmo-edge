// AlgDataQueueDistributor — Alg Data Queue Distributor implementation.

#include "flow/common/AlgDataQueueDistributor.h"

#include <algorithm>

#include "util/VideoInfo.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

AlgDataQueueDistributor::AlgDataQueueDistributor(const std::string& name) : name_(name) {
    LOG_INFO("[{}] Init", name_);
}

AlgDataQueueDistributor::~AlgDataQueueDistributor() {
    if (!tasks_.empty()) {
        LOG_ERRO("[{}] Wait For Destroy, But It Still Have SubTask.Task:{}", name_, tasks_.size());
    }
    LOG_INFO("[{}] Destroy", name_);
}

void AlgDataQueueDistributor::UpdateSubTask(AlgDataTask& taskGroup, AlgTaskUnit& newTask) {
    auto it = std::find_if(taskGroup.tasks.begin(), taskGroup.tasks.end(), [&](const auto& task) {
        return (newTask.channel_id == task.channel_id) && (newTask.task_id == task.task_id) &&
               (newTask.flowActionId == task.flowActionId);
    });
    if (it != taskGroup.tasks.end()) {
        // Update fps
        it->fps = newTask.fps;
        // Update the sub-task's queue pointer to prevent holding the old queue and causing a memory leak
        if (newTask.que && it->que != newTask.que) {
            it->que = newTask.que;
        }
        UpdateTaskMaxFps(taskGroup);
        LOG_INFO("[{}] Modify Queue:{}/{} Action:{} FlowId:{} Update Fps:{}, QueName:{}", name_,
                 newTask.channel_id, newTask.task_id, newTask.actionId, newTask.flowActionId, newTask.fps,
                 newTask.que->Name());
        return;
    }

    taskGroup.tasks.push_back(newTask);
    UpdateTaskMaxFps(taskGroup);
    LOG_INFO("[{}] Add Queue:{}/{} Action:{} FlowId:{} Update Fps:{}, QueName:{}", name_, newTask.channel_id,
             newTask.task_id, newTask.actionId, newTask.flowActionId, newTask.fps, newTask.que->Name());
    return;
}

bool AlgDataQueueDistributor::OutQueueExist(AlgTaskUnit& newTask) {
    for (auto& taskGroup : tasks_) {
        if (!taskGroup.que) {
            continue;
        }

        if (taskGroup.que->Name() == newTask.que->Name()) {
            // After a task is switched off and on, AlgActionBase::Start() will recreate the data_queue.
            // At this point, newTask.que points to the new queue, but taskGroup.que still points to the old
            // stopped queue. We must update taskGroup.que, otherwise the shared_ptr of the old queue will be
            // held forever, causing a memory leak, and the new queue will not receive any data.
            if (taskGroup.que != newTask.que) {
                LOG_INFO("[{}] Queue '{}' pointer updated (old stopped queue released)", name_,
                         newTask.que->Name());
                taskGroup.que = newTask.que;
            }
            UpdateSubTask(taskGroup, newTask);
            return true;
        }
    }

    return false;
}

bool AlgDataQueueDistributor::RegistProcQueue(AlgTaskUnit newTask) {
    if (!newTask.que) {
        LOG_WARN("[{}] Add Queue:{}/{} Action:{} FlowId:{} Fps:{} But The Queue Is Empty", name_,
                 newTask.channel_id, newTask.task_id, newTask.actionId, newTask.flowActionId, newTask.fps);
        return false;
    }
    // When less than 0, do full frame detection, set a very large frame rate
    if (newTask.fps < 0) {
        newTask.fps = media::kFpsCtrlMaxFps;
    }

    std::lock_guard<std::shared_mutex> lock(task_mtx_);
    if (OutQueueExist(newTask)) {
        return true;
    }

    AlgDataTask taskGroup;
    taskGroup.group_id     = util::GenerateUUID();
    taskGroup.que          = newTask.que;
    taskGroup.max_task_fps = newTask.fps;
    if (newTask.fps > max_fps_) {
        max_fps_ = newTask.fps;
    }
    sign_ += 1;
    taskGroup.tasks.push_back(newTask);
    tasks_.push_back(taskGroup);
    LOG_INFO("[{}] groupId:{} Add Queue:{}/{} Action:{} FlowId:{} Fps:{}, QueName:{} SubTaskSize:{}", name_,
             taskGroup.group_id, newTask.channel_id, newTask.task_id, newTask.actionId, newTask.flowActionId,
             newTask.fps, newTask.que->Name(), tasks_.size());
    return true;
}

bool AlgDataQueueDistributor::RemoveProcQueue(AlgTaskUnit newTask) {
    bool bNeedRmvGroup = false;
    std::string groupId;
    std::lock_guard<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        auto it = std::find_if(taskGroup.tasks.begin(), taskGroup.tasks.end(), [&](const AlgTaskUnit& task) {
            return ((task.channel_id == newTask.channel_id) && (task.task_id == newTask.task_id) &&
                    (task.actionId == newTask.actionId) && (task.flowActionId == newTask.flowActionId));
        });
        if (it != taskGroup.tasks.end()) {
            sign_ += 1;
            taskGroup.tasks.erase(it);
            UpdateMaxFps();
            LOG_INFO("{} groupId:{} Remove Queue:{}/{} Action:{} FlowId:{} Success", name_,
                     taskGroup.group_id, newTask.channel_id, newTask.task_id, newTask.actionId,
                     newTask.flowActionId);
            if (taskGroup.tasks.empty()) {
                bNeedRmvGroup = true;
                groupId       = taskGroup.group_id;
                break;
            }
            return true;
        }
    }

    if (bNeedRmvGroup) {
        auto it = std::find_if(tasks_.begin(), tasks_.end(),
                               [&](const AlgDataTask& task) { return task.group_id == groupId; });
        if (it != tasks_.end()) {
            sign_ += 1;
            tasks_.erase(it);
            LOG_INFO("{} Remove Queue Group {} Success", name_, groupId);
            return true;
        }
    }

    // Exact four-field match failed; try fallback match by channelId + taskId only.
    // This handles cases where flowActionId was regenerated (e.g., task refresh in TaskStart).
    for (auto itGroup = tasks_.begin(); itGroup != tasks_.end(); ++itGroup) {
        auto& taskGroup = *itGroup;
        auto it = std::find_if(taskGroup.tasks.begin(), taskGroup.tasks.end(), [&](const AlgTaskUnit& task) {
            return ((task.channel_id == newTask.channel_id) && (task.task_id == newTask.task_id));
        });
        if (it != taskGroup.tasks.end()) {
            sign_ += 1;
            LOG_WARN(
                "{} groupId:{} Remove Queue:{}/{} Action:{} FlowId:{} via FALLBACK match "
                "(original FlowId:{})",
                name_, taskGroup.group_id, newTask.channel_id, newTask.task_id, it->actionId,
                it->flowActionId, newTask.flowActionId);
            taskGroup.tasks.erase(it);
            UpdateMaxFps();
            if (taskGroup.tasks.empty()) {
                LOG_INFO("{} Remove Queue Group {} via fallback", name_, taskGroup.group_id);
                tasks_.erase(itGroup);
            }
            return true;
        }
    }

    LOG_WARN("{} Remove Queue:{}/{} Action:{} FlowId:{} Failed", name_, newTask.channel_id, newTask.task_id,
             newTask.actionId, newTask.flowActionId);
    return false;
}

// float AlgDataQueueDistributor::GetTaskMaxFps() {
//     float fps = 0.0;
//     std::shared_lock<std::shared_mutex> lock(task_mtx_);
//     for (auto& taskGroup : tasks_) {
//         for (auto& task : taskGroup.tasks) {
//             if (task.fps > fps) {
//                 fps = task.fps;
//             }
//         }
//     }

// }

float AlgDataQueueDistributor::GetMaxFps() {
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    return max_fps_;
}

int AlgDataQueueDistributor::DistributorData(AlgDataPtr Data) {
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!Data) {
        return 0;
    }
    int msgCount = 0;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        if (out_fps_ctl_.IsFilter(data_index_, taskGroup.max_task_fps,
                                  Data->firstTimePoint)) {  // Frame rate filtering
            continue;
        }

        if (taskGroup.que) {
            msgCount += 1;
            taskGroup.que->Insert(Data);
        }
    }

    return msgCount;
}

int AlgDataQueueDistributor::DistributorData(AlgDataPtr Frame, VideoFramePtr Data,
                                             std::function<AlgDataPtr(AlgDataPtr, VideoFramePtr)> func) {
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!Data) {
        return 0;
    }
    int msgCount      = 0;
    AlgDataPtr inData = nullptr;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        if (out_fps_ctl_.IsFilter(data_index_, taskGroup.max_task_fps,
                                  Frame->firstTimePoint)) {  // Frame rate filtering
            continue;
        }

        if (taskGroup.que) {
            if (nullptr == inData) {
                inData = func(Frame, Data);
            }
            msgCount += 1;
            taskGroup.que->Insert(inData);
        }
    }

    return msgCount;
}

// Only send to the channel registered by the task. Used for detection data distribution.
// When the detector is multiplexed to multiple channels, those of the same channel are distributed through
// channelId.
int AlgDataQueueDistributor::DistributorData(const std::string& channelId, AlgDataPtr Data,
                                             std::function<AlgDataPtr(AlgDataPtr, std::string)> func) {
    in_fps_ = input_fps_calc_.Fps();
    data_index_++;
    UpdateCtrlFps();
    if (!Data) {
        return 0;
    }
    int msgCount = 0;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        if (out_fps_ctl_.IsFilter(data_index_, taskGroup.max_task_fps,
                                  Data->firstTimePoint)) {  // Frame rate filtering
            continue;
        }

        LOG_DEBUG("{}/{} Task:{} [Distribute] groupId:{} Frames.  Data:{}", name_, channelId,
                  taskGroup.group_id, data_index_, Data->dataType);

        for (auto& task : taskGroup.tasks) {
            if (channelId == task.channel_id) {
                if (taskGroup.que) {
                    msgCount += 1;
                    auto inData = func(Data, task.task_id);
                    if (inData)
                        taskGroup.que->Insert(inData);
                }
            }
        }
    }

    return msgCount;
}

void AlgDataQueueDistributor::UpdateMaxFps() {
    float maxFps = 0.0;

    for (auto& unit : tasks_) {
        UpdateTaskMaxFps(unit);
        if (unit.max_task_fps > maxFps)
            maxFps = unit.max_task_fps;
    }
    max_fps_ = maxFps;
    return;
}

void AlgDataQueueDistributor::UpdateTaskMaxFps(AlgDataTask& taskGroup) {
    float maxFps = 0.0;
    if (!taskGroup.tasks.empty()) {
        auto it = std::max_element(taskGroup.tasks.begin(), taskGroup.tasks.end(),
                                   [](const auto& a, const auto& b) { return a.fps < b.fps; });
        maxFps  = it->fps;
    }
    taskGroup.max_task_fps = maxFps;
}

void AlgDataQueueDistributor::UpdateCtrlFps() {
    // Update once every 100 frames; can be changed to timed update in the future
    if ((data_index_ < 200) || (0 == data_index_ % 100)) {
        double infps   = in_fps_;
        double absDiff = std::abs(infps - out_fps_ctl_.GetRealFps());
        if (absDiff > 0.1) {
            out_fps_ctl_.SetRealFps(in_fps_);
        }
    }
}

std::vector<AlgDataTask> AlgDataQueueDistributor::GetBindTasks() {
    std::vector<AlgDataTask> bindTasks;
    std::shared_lock<std::shared_mutex> lock(task_mtx_);
    for (auto& taskGroup : tasks_) {
        AlgDataTask bindChannel;
        bindChannel.group_id     = taskGroup.group_id;
        bindChannel.max_task_fps = taskGroup.max_task_fps;
        bindChannel.que          = taskGroup.que;
        for (auto& task : taskGroup.tasks) {
            AlgTaskUnit bindTask;
            bindTask.channel_id   = task.channel_id;
            bindTask.task_id      = task.task_id;
            bindTask.actionId     = task.actionId;
            bindTask.flowActionId = task.flowActionId;
            bindTask.fps          = task.fps;
            bindTask.que          = task.que;
            bindChannel.tasks.push_back(bindTask);
        }
        bindTasks.push_back(bindChannel);
    }

    return bindTasks;
}

int AlgDataQueueDistributor::ForceRemoveByTaskId(const std::string& taskId) {
    int removedCount = 0;
    std::lock_guard<std::shared_mutex> lock(task_mtx_);

    for (auto& taskGroup : tasks_) {
        auto it = taskGroup.tasks.begin();
        while (it != taskGroup.tasks.end()) {
            if (it->task_id == taskId) {
                LOG_INFO("{} ForceRemove Queue taskId:{} channelId:{} Action:{} FlowId:{}", name_,
                         it->task_id, it->channel_id, it->actionId, it->flowActionId);
                it = taskGroup.tasks.erase(it);
                sign_ += 1;
                removedCount++;
            } else {
                ++it;
            }
        }
    }

    // Remove empty groups
    auto grpIt = tasks_.begin();
    while (grpIt != tasks_.end()) {
        if (grpIt->tasks.empty()) {
            LOG_INFO("{} ForceRemove empty group {}", name_, grpIt->group_id);
            grpIt = tasks_.erase(grpIt);
        } else {
            ++grpIt;
        }
    }

    if (removedCount > 0) {
        UpdateMaxFps();
        LOG_INFO("{} ForceRemoveByTaskId:{} removed {} entries, remaining groups:{}", name_, taskId,
                 removedCount, tasks_.size());
    }
    return removedCount;
}

}  // namespace cosmo
