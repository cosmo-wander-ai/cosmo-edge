// TaskServiceImpl — Task service implementation — lifecycle, orchestration, query,

#include "service/task/impl/TaskServiceImpl.h"

#include <algorithm>
#include <iterator>
#include <mutex>

#include "flow/channel/AlgChannel.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/task/TaskBase.h"
#include "flow/task/TaskBaseParam.h"
#include "service/algorithm/IActionService.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/detail/ServiceRegistry.h"
#include "service/network/IClientMessageService.h"
#include "service/system/IAppInfoService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/AlgDataQueueTypes.h"

namespace cosmo::service {

TaskServiceImpl::TaskServiceImpl() : task_base_(std::make_unique<cosmo::TaskBase>()) {
    LOG_INFO("{}", "TaskServiceImpl Init");
}

TaskServiceImpl::~TaskServiceImpl() {
    LOG_INFO("{}", "TaskServiceImpl Delete");
}

void TaskServiceImpl::QueueStatus(std::vector<cosmo::AlgActionDataQueueStatus>& queStatus,
                                  unsigned int duration_sec) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    task_base_->QueueStatus(queStatus, duration_sec);
}

void TaskServiceImpl::PacketStatus(size_t& total, size_t& proc, size_t& discard, size_t& discardMaxSec) {
    total         = 0;
    proc          = 0;
    discard       = 0;
    discardMaxSec = 0;
    std::string action_id;
    std::vector<cosmo::AlgActionDataQueueStatus> que_statuses;
    // Box uses the last 10 seconds of data; quality scoring requires 5 consecutive seconds of packet loss
    QueueStatus(que_statuses, 15);

    for (auto& que_el : que_statuses) {
        // Do not count demux and decode packet loss
        if ((que_el.actionId == "BA_00001 DEMUX") || (que_el.actionId == "BA_00001 DECODE")) {
            continue;
        }

        total += que_el.queueStatus.status.insertCountPeriod;
        proc += que_el.queueStatus.status.processCountPeriod;
        discard += que_el.queueStatus.status.discardCountPeriod;
        if (discardMaxSec < que_el.queueStatus.status.continuousDiscardCountPeriod) {
            discardMaxSec = que_el.queueStatus.status.continuousDiscardCountPeriod;
            action_id     = que_el.actionId;
        }
    }
    if (discardMaxSec > 0) {
        LOG_INFO("action_id:{} discardMaxSec:{}", action_id, discardMaxSec);
    }
}

void TaskServiceImpl::ActionInfo(std::vector<cosmo::ActionRuntimeInfo>& actionInfo) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    task_base_->ActionInfo(actionInfo);
}

cosmo::util::ErrorEnum TaskServiceImpl::TaskCreate(const std::string& channelId,
                                                   const std::string& channel_name, const std::string& taskId,
                                                   cosmo::ActionAlgPtr actionAlg) {
    if (channelId.empty() || taskId.empty()) {
        LOG_INFO("Channel:{} Or Task:{} Empty", channelId, taskId);
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    LOG_INFO("Task:{} TaskCreate begin", taskId);
    std::lock_guard<std::shared_mutex> lock(mtx_);
    LOG_INFO("Task:{} TaskCreate end", taskId);
    auto it = tasks_.find(taskId);
    if (it != tasks_.end()) {
        LOG_WARN("Task:{} In Pool, Cant Repeat.", taskId);
        return cosmo::util::ErrorEnum::Created;
    }

    auto task_el = task_base_->TaskCreate(channelId, channel_name, taskId, actionAlg);
    if (!task_el) {
        LOG_WARN("Task:{} Create Failed.", taskId);
        return cosmo::util::ErrorEnum::ActionFailed;
    }

    tasks_[taskId] = task_el;
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum TaskServiceImpl::TaskDelete(const std::string& taskId) {
    if (taskId.empty()) {
        LOG_INFO("Task:{} Empty", taskId);
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    cosmo::TaskElementPtr task_el = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        auto it = tasks_.find(taskId);
        if (it != tasks_.end()) {
            task_el = it->second;
            tasks_.erase(it);
        } else {
            LOG_WARN("Task:{} Not Found", taskId);
            return cosmo::util::ErrorEnum::NotInit;
        }
    }
    // Ensure task is fully stopped before deletion (thread terminated, queue unregistered)
    if (task_el->is_started) {
        task_base_->TaskStop(task_el);
    }
    if (!task_base_->TaskDelete(task_el)) {
        LOG_WARN("Delete {} Failed", taskId);
        return cosmo::util::ErrorEnum::Failed;
    }
    // Clean up stale entries in log rate-limiting map
    {
        std::lock_guard<std::mutex> lk(log_throttle_mtx_);
        not_in_pool_log_ts_.erase(taskId);
    }
    return cosmo::util::ErrorEnum::Success;
}

bool TaskServiceImpl::TaskStart(const std::string& channelId, const std::string& taskId) {
    // --- Phase 1: Collect refresh info inside lock, no slow operations ------
    cosmo::TaskElementPtr task_el  = nullptr;
    cosmo::TaskElementPtr old_task = nullptr;
    bool need_refresh              = false;
    cosmo::MsgTaskConfig cached_param;
    std::string channel_name;
    std::string old_version;
    std::string new_version;
    cosmo::ActionAlgPtr latest_alg;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        auto it = tasks_.find(taskId);
        if (it == tasks_.end()) {
            LOG_WARN("Channel:{} Task:{} Not In Pool, Cant Start.", channelId, taskId);
            return false;
        }

        // Task not running; before starting, check if algorithm has been updated; if so, rebuild task
        // instance to use latest orchestration/prompts.
        auto task = it->second;
        if (task && !task->is_started) {
            const auto algorithm_id = task->GetAlgId();
            if (!algorithm_id.empty() && algorithm_id != "Channel") {
                latest_alg = ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithm(algorithm_id);
                if (latest_alg && task->GetVersion() != latest_alg->algorithmUpdateTime) {
                    cached_param = task->params;
                    channel_name = task->channelName;
                    old_version  = task->GetVersion();
                    new_version  = latest_alg->algorithmUpdateTime;
                    old_task     = task;
                    need_refresh = true;
                }
            }
        }

        if (!need_refresh) {
            task_el = it->second;
        }
    }
    // --- Lock released ---

    // No algorithm refresh needed; start current task directly
    if (!need_refresh) {
        return task_base_->TaskStart(task_el);
    }

    // --- Phase 2: Perform slow Delete(old) -> Create(new) outside lock ---
    // Must delete old instance first: action manager keys by channel+task; not deleting old causes new
    // registration to fail
    if (!task_base_->TaskDelete(old_task)) {
        LOG_WARN("Channel:{} Task:{} Refresh Before Start Failed On Delete. version:{}->{}", channelId,
                 taskId, old_version, new_version);
        return false;
    }
    old_task.reset();

    auto new_task = task_base_->TaskCreate(channelId, channel_name, taskId, latest_alg);
    if (!new_task) {
        LOG_WARN("Channel:{} Task:{} Refresh Before Start Failed On Create. version:{}->{}", channelId,
                 taskId, old_version, new_version);
        return false;
    }
    new_task->params = cached_param;
    if (!cached_param.params.empty() || !cached_param.areas.empty() || !cached_param.shieldedAreas.empty()) {
        task_base_->ModifyTaskParam(new_task, cached_param);
    }
    LOG_INFO("Channel:{} Task:{} Refreshed Algorithm Before Start. version:{}->{}", channelId, taskId,
             old_version, new_version);

    // --- Phase 3: Briefly lock to write new instance back to map ---
    // Note: must extract old shared_ptr and release it outside lock.
    // Otherwise tasks_[taskId] = new_task triggers old TaskElement destructor inside lock -> ~Qwen3VLWorker
    // -> Stop() -> pthread_join which would block.
    cosmo::TaskElementPtr stale_ref;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        stale_ref      = std::move(tasks_[taskId]);
        tasks_[taskId] = new_task;
    }
    // Explicitly clear action references in the stale task element to break any
    // remaining shared_ptr chains before the TaskElement itself is destroyed.
    if (stale_ref) {
        for (auto& ta_node : stale_ref->actions) {
            ta_node.fatherAction.reset();
            ta_node.actionInst.reset();
        }
    }
    stale_ref.reset();  // Old instance destructs here, no locks held

    // --- Phase 4: Start new task outside lock ---
    return task_base_->TaskStart(new_task);
}

bool TaskServiceImpl::TaskStop(const std::string& taskId) {
    cosmo::TaskElementPtr task_el = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        auto it = tasks_.find(taskId);
        if (it == tasks_.end()) {
            LOG_WARN("Task:{} Not In Pool, Cant Stop.", taskId);
            return false;
        }
        task_el = it->second;
        if (!task_el->is_started) {
            return true;
        }
        // Do not set is_started = false here; TaskBase::TaskStop manages that
    }

    // Sync algorithm params to algorithm instance
    return task_base_->TaskStop(task_el);
}

void TaskServiceImpl::TaskChannelSetUrl(const std::string& channelId, const std::string& url) {
    auto channel = GetChannelInst(channelId);
    if (channel) {
        channel->SetUrl(url);
    }
}

void TaskServiceImpl::TaskChannelRepeatCount(const std::string& channelId, int video_repeat_count) {
    auto channel = GetChannelInst(channelId);
    if (channel) {
        channel->SetVideoRepeatCount(video_repeat_count);
    }
}

void TaskServiceImpl::TaskChannelSetParam(const std::string& channelId, const std::string& url,
                                          int video_repeat_count) {
    auto channel = GetChannelInst(channelId);
    if (channel) {
        channel->SetUrl(url);
        channel->SetVideoRepeatCount(video_repeat_count);
    }
}

VideoFramePtr TaskServiceImpl::CaptureImage(const std::string& channelId, int timeout_ms) {
    auto channel = GetChannelInst(channelId);
    if (channel) {
        return channel->CaptureImage(timeout_ms);
    }

    return nullptr;
}

bool TaskServiceImpl::TaskIsStart(const std::string& taskId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        return false;
    }
    return task_base_->TaskIsStart(it->second);
}

bool TaskServiceImpl::LogicTest(const std::string& taskId, cosmo::MsgTarget& target) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return task_base_->LogicTest(taskId, target);
}

std::vector<std::string> TaskServiceImpl::QueryTasks(bool started) {
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

bool TaskServiceImpl::SetTaskParam(const std::string& channelId, const std::string& taskId,
                                   cosmo::MsgTaskConfig& param) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        LOG_WARN("Channel:{} Task:{} Not In Pool, Cant SetParam.", channelId, taskId);
        return false;
    }
    auto task = it->second;

    for (auto& action_key_param : param.params) {
        auto keys = cosmo::util::Split(action_key_param.key.ToRefString(), ".");
        action_key_param.keys.assign(keys.begin(), keys.end());
    }

    cosmo::AreaToLocal(param);

    // Algorithm parameter settings
    task->params = param;

    // Sync algorithm params to algorithm instance
    return task_base_->ModifyTaskParam(task, param);
}

bool TaskServiceImpl::GetTaskParam(const std::string& channelId, const std::string& taskId,
                                   cosmo::MsgTaskConfig& param) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) {
        LOG_WARN("Channel:{} Task:{} Not In Pool, Cant GetParam.", channelId, taskId);
        return false;
    }
    param = it->second->params;
    return true;
}

std::vector<cosmo::TaskStatus> TaskServiceImpl::GetTaskStatus(const std::vector<std::string>& tasks,
                                                              unsigned int duration_sec) {
    std::vector<cosmo::TaskStatus> task_statuses;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& kv : tasks_) {
        const auto& task = kv.second;
        cosmo::TaskStatus task_status_el;
        if (!task)  // Skip tasks that failed to create
        {
            continue;
        }
        if (!tasks.empty()) {
            if (std::find(tasks.begin(), tasks.end(), task->taskId) == tasks.end()) {
                continue;
            }
        }
        task_status_el.channelId        = task->channelId;
        task_status_el.taskId           = task->taskId;
        task_status_el.algorithmName    = task->GetAlgName();
        task_status_el.algorithmId      = task->GetAlgId();
        task_status_el.algorithmVersion = task->GetVersion();
        for (auto& action : task->actions) {
            if (action.actionInst) {
                // Flow layer uses raw types; convert to DTO after collection
                std::vector<cosmo::AlgActionDataQueueStatus> flow_que_status;
                action.actionInst->QueueStatus(flow_que_status, duration_sec);
                for (auto& qs : flow_que_status) {
                    task_status_el.queStatus.push_back(cosmo::AlgActionDataQueueStatusDto(qs));
                }
                action.actionInst->ActionInfo(task_status_el.actionInfo);
            }
            // Get Url
        }
        task_statuses.push_back(task_status_el);
    }

    return task_statuses;
}

// Task query and status — moved to TaskServiceQuery.cc

// ---------------------------------------------------------------------------
// DTO conversion helpers (moved from VideoTaskServiceImpl)
// ---------------------------------------------------------------------------

namespace {

    cosmo::AlgDataQueueInfoDto ToQueueInfoDto(const cosmo::AlgDataQueueInfo& src) {
        cosmo::AlgDataQueueInfoDto dto;
        dto.name      = src.name;
        dto.queSize   = src.queSize;
        dto.queLength = src.queLength;

        dto.insertCount  = src.status.insertCount;
        dto.processCount = src.status.processCount;
        dto.discardCount = src.status.discardCount;

        dto.periodMs                     = src.status.periodMs;
        dto.durationIndex                = src.status.durationIndex;
        dto.insertCountPeriod            = src.status.insertCountPeriod;
        dto.processCountPeriod           = src.status.processCountPeriod;
        dto.discardCountPeriod           = src.status.discardCountPeriod;
        dto.continuousDiscardCountPeriod = src.status.continuousDiscardCountPeriod;

        dto.continuousDiscardCount    = src.status.continuousDiscardCount;
        dto.continuousDiscardCountMax = src.status.continuousDiscardCountMax;

        dto.holdCount    = src.status.holdCount;
        dto.holdCountMax = src.status.holdCountMax;
        return dto;
    }

    cosmo::AlgActionDataQueueStatusDto ToDto(const cosmo::AlgActionDataQueueStatus& src) {
        cosmo::AlgActionDataQueueStatusDto dto;
        dto.channelIds    = src.channelIds;
        dto.taskIds       = src.taskIds;
        dto.actionId      = src.actionId;
        dto.alarmCount    = src.alarmCount;
        dto.actionStatus  = src.actionStatus;
        dto.queueStatus   = ToQueueInfoDto(src.queueStatus);
        dto.durationInfos = src.durationInfos;
        return dto;
    }

}  // namespace

void TaskServiceImpl::QueueStatusDto(std::vector<cosmo::AlgActionDataQueueStatusDto>& que_status,
                                     unsigned int duration_sec) {
    std::vector<cosmo::AlgActionDataQueueStatus> flow_status;
    QueueStatus(flow_status, duration_sec);
    que_status.reserve(flow_status.size());
    std::transform(flow_status.begin(), flow_status.end(), std::back_inserter(que_status),
                   [](const auto& s) { return ToDto(s); });
}

// ---------------------------------------------------------------------------
// Task Process Orchestration (moved from VideoTaskServiceImpl)
// ---------------------------------------------------------------------------

cosmo::MsgTaskCreateSend TaskServiceImpl::ProcessTaskCreate(cosmo::MsgTaskCreateRecv& data,
                                                            std::error_condition& errc) {
    cosmo::MsgTaskCreateSend ret_data{};
    LOG_INFO("[TaskOP] :{}/{} Create. Running {} Tasks", data.videoChannelId, data.taskId, TaskCount());

    if ((data.mvDebug != cosmo::key::DEBUG_STRING) &&
        (!service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetHaveManager())) {
        errc = cosmo::util::ErrorEnum::MvDebugModel;
        return ret_data;
    }

    // 1. Get orchestration template
    auto action_alg = ServiceRegistry::Instance().Get<IActionService>().GetActionAlg(
        data.algorithmCode, data.algorithmUpdateTime);
    if (!action_alg) {
        // 1.1. Get algorithm orchestration from server
        cosmo::CMsgAlgorithmProcessConfigNGRsp action_alg_msg;
        cosmo::CMsgAlgorithmProcessConfigNGReq action_alg_req;
        action_alg_req.algorithmCode = data.algorithmCode;
        if (!ServiceRegistry::Instance().Get<IClientMessageService>().FetchAlgorithmConfig(action_alg_req,
                                                                                           action_alg_msg)) {
            errc = cosmo::util::ErrorEnum::ActionAlgDownLoadFailed;
            LOG_WARN("AlgCode:{} Name:{} Version:{} Download Failed", data.algorithmCode, data.algorithmName,
                     data.algorithmUpdateTime);
            return ret_data;
        }
        // 1.2. Check atomic algorithms and download them
        // 1.3. Update algorithm orchestration to local [memory]
        ServiceRegistry::Instance().Get<IActionService>().UpdateActionAlg(action_alg_msg.resData);
        // 1.4. Get orchestration template
        action_alg = ServiceRegistry::Instance().Get<IActionService>().GetActionAlg(data.algorithmCode,
                                                                                    data.algorithmUpdateTime);
        if (!action_alg) {
            errc = cosmo::util::ErrorEnum::ActionAlgLoadFailed;
            LOG_WARN("AlgCode:{} Name:{} Version:{} Load Failed", data.algorithmCode, data.algorithmName,
                     data.algorithmUpdateTime);
            return ret_data;
        }
    }

    if (data.streamUrl.empty()) {
        cosmo::CMsgGetVideoPlayReq get_video_req;
        cosmo::CMsgGetVideoPlayRsp get_video_rsp;
        get_video_req.videoChannelId = data.videoChannelId;
        if (!ServiceRegistry::Instance().Get<IClientMessageService>().FetchVideoPlayUrl(get_video_req,
                                                                                        get_video_rsp)) {
            LOG_WARN("{}/{}  AlgCode:{} Name:{} Version:{} GetVideoPlay Failed", data.videoChannelId,
                     data.taskId, data.algorithmCode, data.algorithmName, data.algorithmUpdateTime);
        } else {
            LOG_INFO("{}/{}  AlgCode:{} Name:{} GetVideoPlay Url:{} ", data.videoChannelId, data.taskId,
                     data.algorithmCode, data.algorithmName, get_video_rsp.resData.streamUrl);
            data.streamUrl = get_video_rsp.resData.streamUrl;
        }
    }

    RecordClearTaskData(data.taskId);
    RecordTaskInfo(data.taskId, data);
    RecordTaskAction(data.taskId, action_alg);

    LOG_INFO("{}/{} Create {} Task, algorithmCode:{} algorithmUpdateTime:{}", data.videoChannelId,
             data.taskId, action_alg->algorithmName, action_alg->algorithmCode,
             action_alg->algorithmUpdateTime);

    // 2. Create task
    auto task_create_status = TaskCreate(data.videoChannelId, data.videoChannelName, data.taskId, action_alg);
    // Already created or success
    if ((task_create_status != cosmo::util::ErrorEnum::Success) &&
        (task_create_status != cosmo::util::ErrorEnum::Created)) {
        errc = task_create_status;
        LOG_WARN("AlgCode:{} Name:{} Version:{} Create Failed", data.algorithmCode, data.algorithmName,
                 data.algorithmUpdateTime);
        return ret_data;
    }
    // 3. Set task parameters
    LOG_INFO("{}/{} Create {} Task, Set Params streamUrl:{}", data.videoChannelId, data.taskId,
             action_alg->algorithmName, data.streamUrl);
    if (!data.streamUrl.empty()) {
        cosmo::MsgDynamicKeyValue url;
        url.key   = std::string(cosmo::key::CHANNEL_URL);
        url.value = data.streamUrl;
        data.taskConfig.params.push_back(url);
    }

    SetTaskParam(data.videoChannelId, data.taskId, data.taskConfig);

    if (task_create_status ==
        cosmo::util::ErrorEnum::Success) {  // Only start newly created tasks; existing tasks need no restart
                                            // 4. Start task
        LOG_INFO("{}/{} Start {} Task", data.videoChannelId, data.taskId, action_alg->algorithmName);
        TaskStart(data.videoChannelId, data.taskId);
    }

    return ret_data;
}

cosmo::MsgTaskCancleSend TaskServiceImpl::ProcessTaskCancel(cosmo::MsgTaskCancleRecv& data,
                                                            std::error_condition& errc) {
    cosmo::MsgTaskCancleSend ret_data{};
    LOG_INFO("[TaskOP] :{} Cancel. Running {} Tasks", data.taskId, TaskCount());

    if ((data.mvDebug != cosmo::key::DEBUG_STRING) &&
        (!service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetHaveManager())) {
        errc = cosmo::util::ErrorEnum::MvDebugModel;
        return ret_data;
    }

    // 1. Delete task
    auto ret = TaskDelete(data.taskId);
    // Success and task-not-started both count as no error
    if (!((cosmo::util::ErrorEnum::Success == ret) || (cosmo::util::ErrorEnum::NotInit == ret))) {
        errc = ret;
    }

    return ret_data;
}

// ---------------------------------------------------------------------------
// Data Recording (moved from VideoTaskServiceImpl)
// ---------------------------------------------------------------------------

void TaskServiceImpl::RecordClearTaskData(const std::string& taskId) {
    cosmo::RecordAlgDataClearTaskData(taskId);
}

void TaskServiceImpl::RecordTaskInfo(const std::string& taskId, cosmo::MsgTaskCreateRecv& data) {
    cosmo::RecordAlgTaskInfo(taskId, data);
}

void TaskServiceImpl::RecordTaskAction(const std::string& taskId, cosmo::ActionAlgPtr data) {
    cosmo::RecordAlgTaskAction(taskId, data);
}

}  // namespace cosmo::service
