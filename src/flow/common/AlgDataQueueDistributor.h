#pragma once

#include <shared_mutex>
#include <vector>

#include "flow/common/AlgDataQueue.h"
#include "flow/common/AlgDataUnit.h"
#include "util/FpsCalc.h"
#include "util/FpsCtrl.h"

namespace cosmo {
struct AlgTaskUnit {
    std::string channel_id;    // channelId of the registrant
    std::string task_id;       // taskId of the registrant
    std::string actionId;      // ActionId of the registrant
    std::string flowActionId;  // Flow ActionId of the registrant
    float fps{-1.0};           // Frame rate of the registrant
    AlgDataType dataType{AlgDataType::ChannelDataDec};
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> que;
};
struct AlgDataTask {
    float max_task_fps{0.0};
    std::string group_id;
    std::shared_ptr<AlgDataQueue<AlgDataPtr>> que;  // Channel multiplexing. E.g., tripwire and intrusion
                                                    // obtain the same detector, but different tasks
    std::vector<AlgTaskUnit> tasks;
};

class AlgDataQueueDistributor {
public:
    explicit AlgDataQueueDistributor(const std::string& moduleName);

    ~AlgDataQueueDistributor();

    // Register task processing queue
    bool RegistProcQueue(AlgTaskUnit newTask);

    bool RemoveProcQueue(AlgTaskUnit newTask);

    // Distribute data to all registered queues
    int DistributorData(AlgDataPtr Data);

    // Filter and distribute data to all registered queues, calling func for conversion after filtering
    int DistributorData(AlgDataPtr Frame, VideoFramePtr Data,
                        std::function<AlgDataPtr(AlgDataPtr, VideoFramePtr)> func);

    // Distribute data to specific registered queues
    // Only send to the channel registered by the task. Used for detection data distribution.
    // When the detector is multiplexed to multiple channels, those of the same channel are distributed
    // through channelId.
    int DistributorData(const std::string& channelId, AlgDataPtr Data,
                        std::function<AlgDataPtr(AlgDataPtr, std::string)> func);

    // // Get the maximum frame rate of detection tasks by polling all tasks
    // float GetTaskMaxFps();

    // Get the maximum frame rate of detection tasks (get recorded value)
    float GetMaxFps();

    void ResetDistributor() {
        std::lock_guard<std::shared_mutex> lock(task_mtx_);
        data_index_ = 0;
        in_fps_     = 0.0;
        input_fps_calc_.Reset();
        out_fps_ctl_.ChangeFps(0.0f, 0.0f);
    }

    std::vector<AlgDataTask> GetBindTasks();

    // Force-remove all queue entries matching taskId, regardless of flowActionId.
    // Used during task deletion to ensure no stale queue references remain.
    int ForceRemoveByTaskId(const std::string& taskId);

    int GetSign() {
        return sign_;
    };

protected:
    inline size_t GetTaskCount() {
        std::shared_lock<std::shared_mutex> lock(task_mtx_);
        return tasks_.size();
    };

private:
    // Check if the newly registered Queue already exists in the queue
    bool OutQueueExist(AlgTaskUnit& newTask);

    // Update the queue
    void UpdateSubTask(AlgDataTask& taskGroup, AlgTaskUnit& newTask);

    // Update maximum frame rate when deleting a node; auto-update when adding a node if new fps is larger
    void UpdateMaxFps();

    // Update the input frame rate of the control frame rate template
    void UpdateCtrlFps();

    void UpdateTaskMaxFps(AlgDataTask& taskUnit);

private:
    std::shared_mutex task_mtx_;
    std::string name_;
    float max_fps_{0.0};            // Maximum required frame rate among task queues
    float in_fps_{0.0};             // Actual frame rate sent to this distribution template
    size_t data_index_{0};          // Actual frame sequence sent to this distribution template
    util::FpsCalc input_fps_calc_;  // Input FPS calculator
    util::FpsCtrl out_fps_ctl_;     // Task frame rate control template
    int sign_{0};  // +1 for both queue registration and unregistration to mark changes in registrants
    std::vector<AlgDataTask> tasks_;
};

using AlgDataQueueDistributorPtr = std::shared_ptr<AlgDataQueueDistributor>;

}  // namespace cosmo
