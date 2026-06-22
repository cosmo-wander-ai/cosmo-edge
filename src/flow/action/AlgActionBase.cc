// AlgActionBase — Alg Action Base implementation.

#include "flow/action/AlgActionBase.h"

#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "util/Log.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

AlgActionBase::AlgActionBase(AlgActionType actionType, ActionNode& action, std::string channel_,
                             std::string taskId, std::string threadName)
    : util::Thread(threadName.empty() ? (taskId + "-" + action.actionName + "-" + action.flowActionId)
                                      : threadName),
      action_type(actionType),
      channel(std::move(channel_)),
      task_id(std::move(taskId)),
      action_node(action),
      duration_stat(threadName.empty() ? (task_id + "-" + action.actionName + "-" + action.flowActionId)
                                       : threadName) {
    std::string name = threadName.empty()
                           ? (task_id + "-" + action_node.actionName + "-" + action_node.flowActionId)
                           : threadName;
    data_queue       = std::make_shared<AlgDataQueue<AlgDataPtr>>(name);
    distributor      = std::make_shared<AlgDataQueueDistributor>(name);

    action_status = util::ErrorEnum::ActionReady;
}

AlgActionBase::~AlgActionBase() {
    data_queue.reset();
    distributor.reset();
}

void AlgActionBase::RemoveActiveTask() {
    int current = m_activeTaskCount.load();
    while (current > 0 && !m_activeTaskCount.compare_exchange_weak(current, current - 1)) {
    }
    if (current <= 0) {
        LOG_WARN("[{}] Action [{}] RemoveActiveTask ignored because activeTaskCount is {}", task_id, Name(),
                 current);
        m_activeTaskCount.store(0);
    }
}

void AlgActionBase::RegisterTaskContext(const std::string& /*taskId*/, ActionAlgPtr alg,
                                        const ActionNode& /*action*/) {
    action_alg = alg;
}

void AlgActionBase::Start() {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    bool expected = false;
    if (running.compare_exchange_strong(expected, true)) {
        // data_queue is set to non-running state in Stop(); if the thread is restarted directly without
        // restoring the queue, Pop() in run() will keep fetching empty, and WaitForData() will return
        // immediately due to !m_isRunning, which leads to busy waiting (high CPU usage).
        if (!data_queue || !data_queue->IsRunning()) {
            size_t oldMaxSize     = data_queue ? data_queue->GetMaxSize() : -1;
            std::string queueName = Name();
            if (queueName.empty()) {
                queueName = task_id + "-" + action_node.actionName + "-" + action_node.flowActionId;
            }
            data_queue = std::make_shared<AlgDataQueue<AlgDataPtr>>(queueName);
            if (oldMaxSize != static_cast<size_t>(-1)) {
                data_queue->SetMaxSize(oldMaxSize);
            }
            LOG_INFO("[{}] Recreate data queue:{} before Start, OldMaxSize:{}", task_id, queueName,
                     oldMaxSize);
        }
        distributor->ResetDistributor();
        if (!start()) {
            running       = false;
            action_status = util::ErrorEnum::ActionStop;
            LOG_ERRO("[{}] Action [{}] start failed: worker thread is still joinable", task_id, Name());
            return;
        }
        action_status = util::ErrorEnum::ActionStart;
    } else {
        LOG_INFO("[{}] Action [{}] already running, skip Start", task_id, Name());
    }
}

void AlgActionBase::Stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mtx_);
    if (running.exchange(false)) {
        if (data_queue) {
            data_queue->Stop();
        }
        stop();
        action_status = util::ErrorEnum::ActionStop;
        LOG_INFO("[{}] AlgActionBase::Stop done, thread joined", task_id);
    } else {
        LOG_INFO("[{}] Action [{}] already stopped, skip Stop", task_id, Name());
    }
}

AlgActionType AlgActionBase::GetActionType() const {
    return action_type;
}

const std::string& AlgActionBase::GetActionId() const {
    return action_node.actionId;
}

const std::string& AlgActionBase::GetFlowActionId() const {
    return action_node.flowActionId;
}

const std::string AlgActionBase::GetAtomicCode() const {
    return action_node.atomicCode.empty() ? action_node.atomAlgName : action_node.atomicCode;
}

const std::string& AlgActionBase::GetChannel() const {
    return channel;
}

const std::string& AlgActionBase::GetTaskId() const {
    return task_id;
}

const std::string& AlgActionBase::GetName() const {
    return action_node.actionName;
}

std::vector<MsgTaskArea> AlgActionBase::GetAssoAreas(const std::vector<MsgTaskArea>& areas) {
    std::vector<MsgTaskArea> assoArea = areas;
    for (auto& area : areas) {
        if (area.bHaveAssoArea) {
            auto assoUnit = GetAssoAreas(area.associatedAreas);
            assoArea.insert(assoArea.end(), assoUnit.begin(), assoUnit.end());
        }
    }
    return assoArea;
}

float AlgActionBase::GetFps() const {
    return 0;
}

bool AlgActionBase::RegistTaskQueue(AlgTaskUnit& param) {
    return distributor->RegistProcQueue(param);
}

bool AlgActionBase::RemoveTaskQueue(AlgTaskUnit& param) {
    return distributor->RemoveProcQueue(param);
}

int AlgActionBase::ForceRemoveByTaskId(const std::string& taskId) {
    return distributor->ForceRemoveByTaskId(taskId);
}

std::shared_ptr<AlgDataQueue<AlgDataPtr>> AlgActionBase::GetQueue() {
    return data_queue;
}

void AlgActionBase::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) {
    if (data_queue) {
        AlgActionDataQueueStatus status;
        auto durationInfo = duration_stat.ComputeStats();
        status.durationInfos.push_back(durationInfo);
        if (!data_queue->Status(status.queueStatus, durationSec))
            return;

        status.actionId = GetActionId();
        status.taskIds.push_back(task_id);
        status.actionStatus = action_status;

        queStatus.push_back(status);
    }
}

void AlgActionBase::ActionInfo(std::vector<ActionRuntimeInfo>& infos) {
    ActionRuntimeInfo actionInfoEl;
    actionInfoEl.actionId = GetActionId();
    auto bindTasks        = distributor->GetBindTasks();
    for (auto& bindTask : bindTasks) {
        for (auto& task : bindTask.tasks) {
            ActionRuntimeSon son;
            son.channelId = task.channel_id;
            son.taskId    = task.task_id;
            son.actionId  = task.actionId;
            actionInfoEl.sons.push_back(son);
        }
    }
    infos.push_back(actionInfoEl);
}

bool AlgActionBase::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                std::vector<MsgDynamicKeyValue>& /*params*/) {
    return false;
}

bool AlgActionBase::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                             std::vector<MsgDynamicKeyValue>& /*params*/) {
    return false;
}

bool AlgActionBase::SetArea(const std::string& /*channelId*/, const std::string& /*taskId*/,
                            std::vector<MsgTaskArea>& /*areas*/,
                            std::vector<MsgTaskArea>& /*shieldedAreas*/) {
    return false;
}

std::vector<DataDetTrackClassify> AlgActionBase::GetHistory(const std::string& /*channelId*/,
                                                            const std::string& /*taskId*/, int64_t /*from*/,
                                                            int64_t /*ts*/, int64_t /*to*/) {
    return {};
}

MsgOverviewMem AlgActionBase::GetOverviewInfo(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                              int64_t /*streamIndex*/, int64_t /*from*/, int64_t /*to*/) {
    MsgOverviewMem info;
    info.type = MsgOverviewMemDataType::MsgOverviewMemDataTypeNone;

    return info;
}

void AlgActionBase::run() {
    while (running) {
        try {
            auto decData = data_queue->Pop();
            if (decData) {
                auto fpsResult = input_fps_calc.FpsWithFrame();
                handle_frame_cnt.store(fpsResult.first, std::memory_order_relaxed);
                in_fps.store(fpsResult.second, std::memory_order_relaxed);
                if (service::ServiceRegistry::Instance().Get<service::IConfigReadService>().GetActionSwitch(
                        GetActionId())) {
                    duration_stat.BeginSample();
                    HandFrame(decData);
                    duration_stat.EndSample();
                }
            } else {
                data_queue->WaitForData();
            }
        } catch (const std::exception& e) {
            LOG_ERRO("[{}] Action [{}] HandFrame exception: {}", task_id, Name(), e.what());
        } catch (...) {
            LOG_ERRO("[{}] Action [{}] HandFrame non-std exception caught", task_id, Name());
        }
    }

    LOG_INFO("[{}] THREAD [{}] Stop ", task_id, Name());
}
}  // namespace cosmo
