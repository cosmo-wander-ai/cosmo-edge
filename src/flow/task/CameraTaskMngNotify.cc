// CameraTaskMngNotify.cc — Algorithm change notification and task query for CameraTaskMng.
// Split from CameraTaskMng.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <utility>

#include "flow/task/CameraTaskMng.h"
#include "flow/task/CameraTaskUnit.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelService.h"
#include "service/task/IScheduleService.h"
#include "service/task/ITaskLifecycle.h"
#include "util/Log.h"

static constexpr const char* kTag = "CAMERA-TASK ";
namespace cosmo {

namespace {
    std::vector<ModelInfo> CollectModelsForAlgorithm(const ActionAlgPtr& algData) {
        std::vector<ModelInfo> models;
        if (!algData) {
            return models;
        }

        auto& modelSvc = service::ServiceRegistry::Instance().Get<service::IModelService>();
        for (const auto& workFlow : algData->workFlow) {
            if (workFlow.atomicCode.empty()) {
                continue;
            }
            auto modelInfo = modelSvc.GetModelInfo(workFlow.atomicCode);
            if (modelInfo.id == workFlow.atomicCode) {
                models.push_back(modelInfo);
            }
        }
        return models;
    }
}  // namespace

std::vector<service::camera::CameraTaskDto> CameraTaskMng::GetTasks() const {
    std::vector<service::camera::CameraTaskDto> taskInfos;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (auto& task : tasks_) {
        service::camera::CameraTaskDto taskInfo;
        taskInfo.taskId        = task->task_id_;
        taskInfo.algorithmCode = task->algorithm_code_;
        taskInfo.algorithmName = task->algorithm_name_;
        taskInfo.scheduleId    = task->schedule_id_;
        taskInfo.scheduleName  = task->schedule_name_;
        taskInfo.enable        = task->is_enabled_;
        taskInfos.push_back(taskInfo);
    }
    return taskInfos;
}

void CameraTaskMng::NotifyAlgorithmChanged(const std::string& algorithmCode, bool restartRunning) {
    if (restartRunning) {
        auto taskIdsToRestart = StopAlgorithmForReload(algorithmCode);
        RebuildAlgorithmForReload(algorithmCode);
        StartTasksAfterReload(taskIdsToRestart);
        return;
    }

    // Update algorithm config under write lock, collect model-derived param refreshes
    struct RefreshRequest {
        CameraTaskUnitPtr taskUnit;
        ActionAlgPtr actionAlg;
    };
    std::vector<RefreshRequest> refreshRequests;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        for (auto& task : tasks_) {
            if (!task || task->algorithm_code_ != algorithmCode) {
                continue;
            }
            // Update algorithm version in structured overlay data to avoid stale state display.
            task->action_alg_ =
                service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetAlgorithm(
                    task->algorithm_code_);
            if (task->task_ && task->task_->IsReady()) {
                refreshRequests.push_back({task->task_, task->action_alg_});
            } else {
                LOG_WARN("[{}/{}] AlgorithmChanged -> skip model refresh because task unit is not ready",
                         channel_id_, task->task_id_);
            }
            // Sync algorithm name so channel task list queries show the latest name
            task->algorithm_name_ =
                service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetAlgorithmName(
                    task->algorithm_code_);
        }
        SaveTaskList();
    }

    // Refresh model-derived task params outside lock (avoid blocking other threads)
    for (const auto& req : refreshRequests) {
        if (req.taskUnit) {
            req.taskUnit->RefreshModels(CollectModelsForAlgorithm(req.actionAlg));
        }
    }
}

std::vector<std::string> CameraTaskMng::StopAlgorithmForReload(const std::string& algorithmCode) {
    std::vector<std::string> taskIdsToRestart;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        for (const auto& task : tasks_) {
            if (!task || task->algorithm_code_ != algorithmCode) {
                continue;
            }
            taskIdsToRestart.push_back(task->task_id_);
        }
    }

    std::vector<std::string> stoppedTaskIds;
    for (const auto& taskId : taskIdsToRestart) {
        if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskIsStart(taskId)) {
            LOG_INFO("[{}/{}] AlgorithmChanged -> stop running task for reload", channel_id_, taskId);
            service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(taskId);
            stoppedTaskIds.push_back(taskId);
        }
    }
    return stoppedTaskIds;
}

void CameraTaskMng::RebuildAlgorithmForReload(const std::string& algorithmCode) {
    CameraTaskPtr taskToRebuild   = nullptr;
    CameraTaskUnitPtr oldTaskUnit = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        for (auto& task : tasks_) {
            if (!task || task->algorithm_code_ != algorithmCode) {
                continue;
            }
            task->action_alg_ =
                service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetAlgorithm(
                    task->algorithm_code_);
            task->algorithm_name_ =
                service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetAlgorithmName(
                    task->algorithm_code_);
            taskToRebuild = task;
            oldTaskUnit   = std::move(task->task_);
            break;
        }
        SaveTaskList();
    }

    if (!taskToRebuild) {
        return;
    }

    oldTaskUnit.reset();
    util::ErrorEnum rebuildRet;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        rebuildRet = MakeTask(taskToRebuild);
    }
    if (rebuildRet != util::ErrorEnum::Success) {
        LOG_WARN("[{}/{}] AlgorithmChanged -> rebuild task failed:{}", channel_id_, algorithmCode,
                 static_cast<uint32_t>(rebuildRet));
        std::lock_guard<std::shared_mutex> lock(mtx_);
        taskToRebuild->task_.reset();
        taskToRebuild->status_ = CameraTaskStatus::kAbnormal;
    } else {
        LOG_INFO("[{}/{}] AlgorithmChanged -> rebuilt task for reload", channel_id_, taskToRebuild->task_id_);
    }
}

void CameraTaskMng::StartTasksAfterReload(const std::vector<std::string>& taskIds) {
    for (const auto& taskId : taskIds) {
        bool restartOk = service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStart(
            channel_id_, taskId);
        std::lock_guard<std::shared_mutex> lock(mtx_);
        auto it = std::find_if(tasks_.begin(), tasks_.end(),
                               [&](const CameraTaskPtr& t) { return t && t->task_id_ == taskId; });
        if (it != tasks_.end()) {
            (*it)->status_ = restartOk ? CameraTaskStatus::kInService : CameraTaskStatus::kAbnormal;
        }
    }
}

void CameraTaskMng::NotifyAlgorithmDeleted(const std::string& algorithmCode) {
    // Phase 1: Mark status and collect taskIds to stop under write lock
    std::vector<std::string> taskIdsToStop;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        for (auto& task : tasks_) {
            if (!task || task->algorithm_code_ != algorithmCode) {
                continue;
            }
            taskIdsToStop.push_back(task->task_id_);
            task->is_enabled_ = false;
            task->status_     = CameraTaskStatus::kStop;
            LOG_WARN("[{}/{}] AlgorithmDeleted -> task disabled", channel_id_, task->task_id_);
        }
        SaveTaskList();
    }

    // Phase 2: Perform expensive TaskStop outside lock
    for (const auto& taskId : taskIdsToStop) {
        if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskIsStart(taskId)) {
            service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(taskId);
        }
    }
}

CameraTaskPtr CameraTaskMng::GetCameraTask(const std::string& algorithmCode) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        return (*it);
    }
    return nullptr;
}

util::ErrorEnum CameraTaskMng::SetStrategySchedule(const std::string& algorithmCode,
                                                   const std::string& scheduleId) {
    std::string scheduleName;
    if (!service::ServiceRegistry::Instance().Get<service::IScheduleService>().Exist(scheduleId,
                                                                                     scheduleName)) {
        return util::ErrorEnum::TimeTemplateNotExist;
    }

    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        if ((*it)->schedule_id_ != scheduleId) {
            (*it)->schedule_name_ = scheduleName;
            (*it)->schedule_id_   = scheduleId;
            SaveTaskList();
        }
        return util::ErrorEnum::Success;
    }
    CameraTaskPtr task    = std::make_shared<CameraTask>();
    task->algorithm_code_ = algorithmCode;

    auto task_ret = MakeTask(task);
    if (util::ErrorEnum::Success != task_ret) {
        return task_ret;
    }
    task->schedule_id_   = scheduleId;
    task->schedule_name_ = scheduleName;
    tasks_.push_back(task);
    SaveTaskList();
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraTaskMng::GetStrategySchedule(const std::string& algorithmCode,
                                                   std::string& scheduleId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        scheduleId = (*it)->schedule_id_;
        return util::ErrorEnum::Success;
    }
    return util::ErrorEnum::TaskNotExist;
}

}  // namespace cosmo
