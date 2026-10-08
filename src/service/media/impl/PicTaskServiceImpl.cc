// PicTaskServiceImpl — Forward declarations — full definitions in flow/task/PTaskBase.h (included in...

#include "service/media/impl/PicTaskServiceImpl.h"

#include <cstdlib>

#include "flow/task/PTaskBase.h"
#include "flow/task/TaskBaseParam.h"
#include "service/algorithm/IActionService.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/detail/ServiceRegistry.h"
#include "service/network/IClientMessageService.h"
#include "service/system/IAppInfoService.h"
#include "service/task/ITaskLifecycle.h"
#include "util/FormatString.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

namespace cosmo::service {

PicTaskServiceImpl::PicTaskServiceImpl() : task_base_(std::make_unique<cosmo::PTaskBase>()) {
    LOG_INFO("{}", "PicTaskServiceImpl Init");
}

PicTaskServiceImpl::~PicTaskServiceImpl() {
    DeleteAllTasksImpl();
    LOG_INFO("{}", "PicTaskServiceImpl Delete");
}

// ── Core Task Lifecycle (migrated from PTaskMng) ──

cosmo::util::ErrorEnum PicTaskServiceImpl::TaskCreate(const std::string& taskId,
                                                      cosmo::ActionAlgPtr action_alg) {
    if (taskId.empty()) {
        LOG_INFO("Task:{} Empty", taskId);
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    if (!action_alg) {
        LOG_WARN("Task:{} Cannot Create Without Algorithm Configuration", taskId);
        return cosmo::util::ErrorEnum::ActionFailed;
    }
    auto task_el = task_base_->TaskCreate(taskId, action_alg);
    if (!task_el)
        return cosmo::util::ErrorEnum::ActionFailed;
    cosmo::PTaskElementPtr old_task;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        if (stopping_)
            return cosmo::util::ErrorEnum::ServiceNotInit;
        const auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            if (it->second->GetVersion() == action_alg->algorithmUpdateTime &&
                it->second->GetAlgId() == action_alg->algorithmCode)
                return cosmo::util::ErrorEnum::Created;
            old_task = it->second;
        }
        tasks_[taskId] = task_el;
    }
    DestroyTask(old_task);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum PicTaskServiceImpl::TaskDelete(const std::string& taskId) {
    if (taskId.empty()) {
        LOG_INFO("Task:{} Empty", taskId);
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    cosmo::PTaskElementPtr task;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        const auto it = tasks_.find(taskId);
        if (it == tasks_.end())
            return cosmo::util::ErrorEnum::NotInit;
        task = it->second;
        tasks_.erase(it);
    }
    DestroyTask(task);
    return cosmo::util::ErrorEnum::Success;
}

bool PicTaskServiceImpl::TaskStart(const std::string& taskId) {
    cosmo::PTaskElementPtr task;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        const auto it = tasks_.find(taskId);
        if (stopping_ || it == tasks_.end())
            return false;
        task = it->second;
    }
    cosmo::PictureTaskLease lease(task);
    return lease && task_base_->TaskActionInit(task);
}

void PicTaskServiceImpl::TaskDeleteAll() {
    DeleteAllTasksImpl();
}

void PicTaskServiceImpl::DeleteAllTasksImpl() {
    // Serialize the whole drain, not only the map swap. A concurrent caller
    // must not return and allow ServiceRegistry teardown while the first caller
    // is still destroying action instances.
    std::lock_guard<std::mutex> shutdown_lock(shutdown_mtx_);
    std::map<std::string, cosmo::PTaskElementPtr> tasks;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        stopping_ = true;
        tasks.swap(tasks_);
    }

    for (const auto& [task_id, task] : tasks) {
        DestroyTask(task);
        LOG_INFO("Delete {} During PicTaskService Shutdown", task_id);
    }
}

void PicTaskServiceImpl::UpdateCheckSum(const std::string& nodeAlgorithmCheckSum) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    node_algorithm_check_sum_ = nodeAlgorithmCheckSum;
}

std::string PicTaskServiceImpl::GetCheckSum() {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return node_algorithm_check_sum_;
}

cosmo::util::ErrorEnum PicTaskServiceImpl::DetectPic(const std::string& taskId,
                                                     cosmo::MsgPTaskDetectPicRecv& data,
                                                     cosmo::MsgPTaskDetectPicSend& retData) {
    if (!data.taskConfig.areas.empty() || !data.taskConfig.shieldedAreas.empty() ||
        (data.resultMode != "business" && data.resultMode != "debug" && data.resultMode != "legacy"))
        return cosmo::util::ErrorEnum::InvalidParam;
    if (taskId.empty()) {
        LOG_INFO("Task:{} Empty", taskId);
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    cosmo::PTaskElementPtr task;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        if (stopping_)
            return cosmo::util::ErrorEnum::ServiceNotInit;
        const auto it = tasks_.find(taskId);
        if (it == tasks_.end())
            return cosmo::util::ErrorEnum::NotCreated;
        task = it->second;
    }
    cosmo::PictureTaskLease lease(task);
    if (!lease)
        return cosmo::util::ErrorEnum::NotCreated;
    if (!data.algorithmCode.empty() && data.algorithmCode != task->GetAlgId())
        return cosmo::util::ErrorEnum::InvalidParam;

    if (!task->is_started) {
        if (task->startFailedCount > 10) {
            LOG_WARN("Task:{} Start Failed More Than 10 Times.", taskId);
            return cosmo::util::ErrorEnum::CreateToManyTimes;
        } else {
            if (!task_base_->TaskActionInit(task)) {
                LOG_WARN("Task:{} Start Failed.", taskId);
                return cosmo::util::ErrorEnum::TaskCreateFailed;
            }
        }
    }

    return task_base_->TaskDetectPic(task, data, retData);
}

// ── Task Process Orchestration ──

cosmo::MsgPTaskCreateSend PicTaskServiceImpl::ProcessPTaskCreate(cosmo::MsgPTaskCreateRecv& data,
                                                                 std::error_condition& errc) {
    cosmo::MsgPTaskCreateSend retData{};
    LOG_INFO("[PTaskOP] : {}/{} Create. Running {} Tasks", data.taskId, data.algorithmCode, TaskCount());
    if (data.taskId.empty()) {
        data.taskId = data.algorithmCode;
    }

    // 1. Get orchestration template from local (algorithmCode is unique identifier)
    auto action_alg = ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithm(data.algorithmCode);
    if (!action_alg) {
        errc = cosmo::util::ErrorEnum::ActionAlgLoadFailed;
        LOG_WARN("AlgCode:{} not found in local algorithm store", data.algorithmCode);
        return retData;
    }

    ServiceRegistry::Instance().Get<ITaskLifecycle>().RecordClearTaskData(data.taskId);
    ServiceRegistry::Instance().Get<ITaskLifecycle>().RecordTaskAction(data.taskId, action_alg);

    LOG_INFO("{} Create {} Task, algorithmCode:{}", data.taskId, action_alg->algorithmName,
             action_alg->algorithmCode);

    // 2. Create task
    auto tast_create_status = TaskCreate(data.taskId, action_alg);
    // Already created or success
    if ((tast_create_status != cosmo::util::ErrorEnum::Success) &&
        (tast_create_status != cosmo::util::ErrorEnum::Created)) {
        errc = tast_create_status;
        LOG_WARN("AlgCode:{} Name:{} Create Failed", data.algorithmCode, action_alg->algorithmName);
        return retData;
    }
    // 3. Set task parameters
    LOG_INFO("{} Create {} Task", data.taskId, action_alg->algorithmName);
    if (!SetTaskParam(data.taskId, data.taskConfig)) {
        errc = cosmo::util::ErrorEnum::InvalidParam;
        TaskDelete(data.taskId);
        return retData;
    }

    if (tast_create_status ==
        cosmo::util::ErrorEnum::Success) {  // Only start newly created tasks; existing tasks need no restart
                                            // 4. Start task
        LOG_INFO("{} Start {} Task", data.taskId, action_alg->algorithmName);
        if (!TaskStart(data.taskId)) {
            errc                      = cosmo::util::ErrorEnum::TaskCreateFailed;
            const auto cleanup_status = TaskDelete(data.taskId);
            if (cleanup_status != cosmo::util::ErrorEnum::Success &&
                cleanup_status != cosmo::util::ErrorEnum::NotInit) {
                LOG_WARN("{} cleanup after start failure returned {}", data.taskId,
                         static_cast<int>(cleanup_status));
            }
        }
    }
    return retData;
}

cosmo::MsgPTaskCancleSend PicTaskServiceImpl::ProcessPTaskCancel(cosmo::MsgPTaskCancleRecv& data,
                                                                 std::error_condition& errc) {
    cosmo::MsgPTaskCancleSend retData{};
    LOG_INFO("[PTaskOP] :{}/{} Cancel. Running {} Tasks", data.taskId, data.algorithmCode, TaskCount());
    if (data.taskId.empty()) {
        data.taskId = data.algorithmCode;
    }
    if ((data.mvDebug != cosmo::key::DEBUG_STRING) &&
        (!service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetHaveManager())) {
        errc = cosmo::util::ErrorEnum::MvDebugModel;
        return retData;
    }

    // 1. Delete task
    auto ret = TaskDelete(data.taskId);
    // Success and task-not-started both count as no error
    if (!((cosmo::util::ErrorEnum::Success == ret) || (cosmo::util::ErrorEnum::NotInit == ret))) {
        errc = ret;
    }

    return retData;
}

cosmo::MsgDetectSend PicTaskServiceImpl::ProcessDetectGroup(cosmo::MsgDetectRecv& data,
                                                            std::error_condition& /*errc*/) {
    cosmo::MsgDetectSend retData{};
    LOG_INFO("[PTask] :Detect Tasks:{}", data.eventCodes.size());
    for (auto& taskUnit : data.eventCodes) {
        int ramdon =
            std::rand() %
            service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetPicTaskGroupCount();
        std::string task_unit_ext = taskUnit + "-" + std::to_string(ramdon);
        cosmo::MsgPTaskDetectPicRecv ptaskUnit;
        ptaskUnit.mvDebug       = data.mvDebug;
        ptaskUnit.taskId        = task_unit_ext;
        ptaskUnit.algorithmCode = taskUnit;
        ptaskUnit.imageUrl      = data.imageUrl;
        ptaskUnit.imageBase64   = data.imageData;
        ptaskUnit.needRetImg    = false;  // No overlay image needed
        ptaskUnit.resultMode    = "legacy";

        std::error_condition unitErrc;
        cosmo::MsgPTaskDetectPicSend ptaskUnitRetData{};

        // Call the underlying DetectPic for single task, not recursively calling Controller
        ptaskUnitRetData.resData.algorithmCode = ptaskUnit.algorithmCode;
        ptaskUnitRetData.resData.timestamp     = std::to_string(cosmo::util::GetMilliseconds());
        unitErrc                               = DetectPic(ptaskUnit.taskId, ptaskUnit, ptaskUnitRetData);

        LOG_INFO("[PTask] :Detect Task:{} Get:{}", taskUnit, unitErrc.message());
        for (const auto& target : ptaskUnitRetData.resData.targetList) {
            // Skip results where logic judgment flow returned false
            if ((target.bHaveLogicResult) && (!target.bLogicResult) && (!data.forceOutputAll)) {
                continue;
            }

            cosmo::MsgDetectEventUnit eventUnit;
            eventUnit.event.eventCode = taskUnit;
            eventUnit.rect.top        = target.box.y;
            eventUnit.rect.left       = target.box.x;
            eventUnit.rect.width      = target.box.width;
            eventUnit.rect.height     = target.box.height;
            for (const auto& confidence : target.confidence) {
                cosmo::MsgDynamicKeyValue attr;
                attr.key   = confidence.label;
                attr.value = COSMO_FORMAT("{:.2f}", confidence.confidence);
                eventUnit.event.eventAttr.push_back(attr);
            }
            retData.data.result.push_back(eventUnit);
        }
    }

    return retData;
}

// ── Query & Parameter Methods (migrated from PTaskMng) ──

std::vector<std::string> PicTaskServiceImpl::QueryTasks(bool started) {
    std::vector<std::string> tasks;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& [id, task] : tasks_) {
        if (task) {
            // Already started algorithm
            if (started) {
                if (task->is_started) {
                    tasks.push_back(id);
                }
            } else {
                tasks.push_back(id);
            }
        }
    }
    return tasks;
}

std::string PicTaskServiceImpl::GetRealTask(const std::string& input) {
    size_t pos = input.find('-');
    if (pos != std::string::npos) {
        // Use substr to extract the substring before '-'
        std::string result = input.substr(0, pos);
        return result;
    }
    return input;
}

std::vector<std::string> PicTaskServiceImpl::QueryRealTasks(bool started) {
    std::vector<std::string> real_tasks;
    auto tasks = QueryTasks(started);
    for (auto& task : tasks) {
        auto real_task = GetRealTask(task);
        if (real_task.empty()) {
            continue;
        }
        if (std::find(real_tasks.begin(), real_tasks.end(), real_task) != real_tasks.end()) {
            continue;
        }
        real_tasks.push_back(real_task);
    }

    return real_tasks;
}

bool PicTaskServiceImpl::SetTaskParam(const std::string& taskId, cosmo::MsgTaskConfig& param) {
    if (!param.areas.empty() || !param.shieldedAreas.empty())
        return false;
    cosmo::PTaskElementPtr task;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        const auto it = tasks_.find(taskId);
        if (stopping_ || it == tasks_.end())
            return false;
        task = it->second;
    }
    cosmo::PictureTaskLease lease(task);
    return lease && task_base_->ModifyTaskParam(task, param);
}

bool PicTaskServiceImpl::GetTaskParam(const std::string& taskId, cosmo::MsgTaskConfig& param) {
    cosmo::PTaskElementPtr task;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        const auto it = tasks_.find(taskId);
        if (it == tasks_.end())
            return false;
        task = it->second;
    }
    cosmo::PictureTaskLease lease(task);
    if (!lease)
        return false;
    param = task->params;
    return true;
}

std::vector<cosmo::PTaskStatus> PicTaskServiceImpl::GetTaskStatus(unsigned int /*durationSec*/) {
    std::vector<cosmo::PTaskStatus> task_statuss;
    return task_statuss;
}

bool PicTaskServiceImpl::TasksHaveChange(const std::string& nodeAlgorithmCheckSum) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return node_algorithm_check_sum_ != nodeAlgorithmCheckSum;
}

size_t PicTaskServiceImpl::TaskCount() {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return tasks_.size();
}

void PicTaskServiceImpl::DestroyTask(const cosmo::PTaskElementPtr& task) noexcept {
    if (!task) {
        return;
    }
    {
        std::unique_lock<std::mutex> lock(task->mtx);
        task->retired = true;
        task->idle.notify_all();
        task->idle.wait(lock, [&] { return !task->executing; });
    }
    try {
        if (task->is_started && !task_base_->TaskActionDestroy(task)) {
            LOG_WARN("Stop {} Actions Warning. Continuing task cleanup.", task->taskId);
        }
    } catch (...) {
        LOG_ERRO("Stop {} Actions Threw. Continuing task cleanup.", task->taskId);
    }
    try {
        if (!task_base_->TaskDelete(task)) {
            LOG_WARN("Delete {} Actions Warning. Forcing success to ensure UI unmount.", task->taskId);
        }
    } catch (...) {
        LOG_ERRO("Delete {} Actions Threw. Continuing shutdown.", task->taskId);
    }
}

}  // namespace cosmo::service
