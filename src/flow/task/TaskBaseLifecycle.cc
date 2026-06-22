// TaskBaseLifecycle.cc — Lifecycle management for TaskBase.
// Split from TaskBase.cc to reduce file size (DEBT-007).

#include "flow/task/TaskBase.h"
#include "util/Log.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

void TaskBase::TaskRegist(TaskElementPtr task) {
    // Register task queues with parent actions
    for (auto& taNode : task->actions) {
        if (taNode.fatherAction) {
            AlgTaskUnit param;
            param.channel_id = task->channelId;
            param.task_id    = task->taskId;
            //	param.actionId = taNode.action.atomicCode;
            param.actionId     = taNode.action.actionId;
            param.flowActionId = taNode.action.flowActionId;
            param.fps          = taNode.action.initFps;
            param.que          = taNode.actionInst ? taNode.actionInst->GetQueue() : nullptr;
            if (taNode.actionInst) {
                LOG_INFO("[{}-{} Create {}] Action: {} Regist To {}, Fps:{}", task->channelId, task->taskId,
                         task->GetAlgName(), taNode.action.actionId, taNode.fatherAction->GetName(),
                         param.fps);
                taNode.fatherAction->RegistTaskQueue(param);
            } else {
                LOG_ERRO("[{}-{} Create {}] Action: {} Regist To {} Failed", task->channelId, task->taskId,
                         task->GetAlgName(), taNode.action.actionId, taNode.fatherAction->GetName());
            }
        } else {
            task->flowActionId = taNode.action.flowActionId;
            LOG_INFO("[{}-{} Create {}] Action: {}  is Root, flowActionId is :{}", task->channelId,
                     task->taskId, task->GetAlgName(), taNode.action.actionId, taNode.action.flowActionId);
        }
    }
}

void TaskBase::TaskUnRegist(TaskElementPtr task) {
    // Unregister task queues from parent actions
    for (auto& taNode : task->actions) {
        if (taNode.fatherAction) {
            AlgTaskUnit param;
            param.channel_id   = task->channelId;
            param.task_id      = task->taskId;
            param.actionId     = taNode.action.actionId;
            param.flowActionId = taNode.action.flowActionId;
            param.fps          = taNode.action.initFps;
            LOG_INFO("[{}-{} Remove {}] Action: {}  RemoveQueue From {}", task->channelId, task->taskId,
                     task->GetAlgName(), taNode.action.actionId, taNode.fatherAction->GetName());
            taNode.fatherAction->RemoveTaskQueue(param);
        }
    }
}

bool TaskBase::TaskDelete(TaskElementPtr task) {
    // Orchestration config is empty
    if (!task) {
        LOG_ERRO("{}", "Delete Task Failed. Task Is Empty");
        return false;
    }

    LOG_INFO("[{}-{} Remove {}]", task->channelId, task->taskId, task->GetAlgName());

    // Release actions
    for (auto& taNode : task->actions) {
        // Force-clean any stale distributor entries for this task from the parent action.
        // This is a safety net: if TaskUnRegist's RemoveProcQueue failed (e.g., flowActionId mismatch),
        // stale shared_ptr<AlgDataQueue> entries would be held forever, causing memory leaks.
        if (taNode.fatherAction) {
            taNode.fatherAction->ForceRemoveByTaskId(task->taskId);
        }
        // Defensively break parent reference chain to ensure action refcount can reach zero
        taNode.fatherAction.reset();

        auto actionInst = taNode.actionInst;
        if (!actionInst) {
            LOG_WARN("[{}-{} Remove {}] ActionInst Is Empty, Skip", task->channelId, task->taskId,
                     task->GetAlgName());
            continue;
        }
        auto it = action_handlers_.find(taNode.action.actionId);
        if (it != action_handlers_.end()) {
            bool instDelRet = it->second.destroy(actionInst, task->channelId, task->taskId);
            if (!instDelRet) {
                LOG_WARN(
                    "[{}-{} Remove {}] Action destroy returned false, force releasing. ID:{} "
                    "ActionName:{} use_count:{}",
                    task->channelId, task->taskId, task->GetAlgName(), taNode.action.actionId,
                    actionInst->GetName(), taNode.actionInst.use_count());
            }
        } else {
            LOG_WARN("[{}-{} Remove {}] Action Unknown, force releasing. ID:{} ActionName:{}",
                     task->channelId, task->taskId, task->GetAlgName(), taNode.action.actionId,
                     actionInst->GetName());
        }

        // Always release the action instance to prevent memory leaks.
        // Even if destroy() failed or the action is unknown, we must not hold the shared_ptr
        // indefinitely — the Mng's internal collection will also release its copy eventually.
        taNode.actionInst.reset();

        LOG_INFO("[{}-{} Remove {}] Action ID:{} ", task->channelId, task->taskId, task->GetAlgName(),
                 taNode.action.actionId);
    }
    LOG_INFO("[{}-{} Remove {}] Ok ", task->channelId, task->taskId, task->GetAlgName());
    return true;
}

void TaskBase::RegisterMngProviders() {
    mng_providers_ = {
        &channel_mng_,
        &detect_mng_,
        &dino_detect_mng_,
        &qwen3_vl_mng_,
        &sam2_segment_mng_,
        &track_mng_adapter_,
        &classify_mng_adapter_,
        &classify_group_mng_adapter_,
        &classify_area_mng_adapter_,
        &classify_attr_mng_adapter_,
        &landmark_mng_adapter_,
        &recognizer_mng_,
        &ai_video_quality_mng_adapter_,
        &filter_mng_,
        &logical_judgment_mng_,
        &sensitivity_mng_,
        &pos_save_sensitivity_mng_,
        &task_alarm_mng_,
        &area_alarm_mng_,
        &face_logic_mng_,
        &task_face_alarm_mng_,
        &action_branch_mng_,
        &target_choose_best_mng_adapter_,
    };
}

void TaskBase::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) {
    for (auto* p : mng_providers_) {
        p->QueueStatus(queStatus, durationSec);
    }
}

void TaskBase::ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) {
    for (auto* p : mng_providers_) {
        p->ActionInfo(actionInfos);
    }
}

bool TaskBase::TaskStart(TaskElementPtr task) {
    if (task->is_started) {
        LOG_INFO("[{}-{} {}] Alread Started", task->channelId, task->taskId, task->GetAlgName());
        return true;
    }

    for (auto& taNode : task->actions) {
        LOG_DEBUG("[{}-{} {}] {} root:{} task:{}", task->channelId, task->taskId, task->GetAlgName(),
                  taNode.action.actionName, task->flowActionId, taNode.action.flowActionId);
        taNode.actionInst->AddActiveTask();
        LOG_INFO("[{}-{} {}] Start {} (activeTaskCount={})", task->channelId, task->taskId,
                 task->GetAlgName(), taNode.action.actionName, taNode.actionInst->GetActiveTaskCount());
        taNode.actionInst->Start();
    }
    TaskRegist(task);
    task->is_started = true;
    return true;
}

bool TaskBase::TaskStop(TaskElementPtr task) {
    if (!task->is_started) {
        LOG_INFO("[{}-{} {}] Not started, skip stop", task->channelId, task->taskId, task->GetAlgName());
        return true;
    }
    TaskUnRegist(task);
    for (auto& taNode : task->actions) {
        LOG_DEBUG("[{}-{} {}] {} root:{} task:{}", task->channelId, task->taskId, task->GetAlgName(),
                  taNode.action.actionName, task->flowActionId, taNode.action.flowActionId);
        taNode.actionInst->RemoveActiveTask();
        int remaining = taNode.actionInst->GetActiveTaskCount();
        if (remaining <= 0) {
            LOG_INFO("[{}-{} {}] Stop {} (no more active tasks)", task->channelId, task->taskId,
                     task->GetAlgName(), taNode.action.actionName);
            taNode.actionInst->Stop();
        } else {
            LOG_INFO("[{}-{} {}] Skip stop {} (activeTaskCount={})", task->channelId, task->taskId,
                     task->GetAlgName(), taNode.action.actionName, remaining);
        }
    }
    task->is_started = false;
    return true;
}

bool TaskBase::TaskIsStart(TaskElementPtr task) {
    return task->is_started;
}

bool TaskBase::LogicTest(const std::string& taskId, const MsgTarget& target) {
    return logical_judgment_mng_.LogicTest(taskId, target);
}

bool TaskBase::ModifyTaskParam(TaskElementPtr task, MsgTaskConfig& taskConfig) {
    // Orchestration config is empty
    if (!task) {
        LOG_ERRO("{}", "Delete Task Failed. Task Is Empty");
        return false;
    }

    // Key: ModifyParam relies on param.keys (e.g. param.faceSet => ["param","faceSet"]).
    // CameraTaskUnit (service/camera/impl/) currently only expands keys for aiParam.xxx.confidence; other
    // params (like param.faceSet) may have empty keys. Backfill keys here before entering actions to avoid
    // AiRecognizer etc. failing due to empty param.keys.
    for (auto& kv : taskConfig.params) {
        if (kv.keys.empty() && !kv.key.empty()) {
            auto parts = util::Split(kv.key.ToRefString(), ".");
            kv.keys.assign(parts.begin(), parts.end());
        }
    }

    for (auto& taNode : task->actions) {
        LOG_INFO("[{}/{} {}] ModifyParam For {}/{} params.size:{} areas.size:{} shieldedAreas.size:{}",
                 task->channelId, task->taskId, task->GetAlgName(), taNode.action.actionId,
                 taNode.action.actionName, taskConfig.params.size(), taskConfig.areas.size(),
                 taskConfig.shieldedAreas.size());
        taNode.actionInst->ModifyParam(task->channelId, task->taskId, taskConfig.params);
        taNode.actionInst->SetArea(task->channelId, task->taskId, taskConfig.areas, taskConfig.shieldedAreas);
    }

    return true;
}

AlgChannelPtr TaskBase::GetChannelInst(const std::string& channelId) {
    return channel_mng_.GetChannelInst(channelId);
}

std::vector<std::string> TaskBase::GetChannelTasks(const std::string& channelId) {
    return channel_mng_.GetChannelTasks(channelId);
}

TaskAlarmPtr TaskBase::GetAlarmInst(const std::string& channelId, const std::string& taskId) {
    return task_alarm_mng_.GetInst(channelId, taskId);
}

void TaskBase::AddTaskChannel(const MsgTaskCreateRecv& taskCreateRecv) {
    channel_mng_.AddTaskChannel(taskCreateRecv);
}

void TaskBase::DeleteTaskChannel(const MsgTaskCancleRecv& taskCancleRecv, const std::string& channelId,
                                 const std::string& algorithmId) {
    channel_mng_.DeleteTaskChannel(taskCancleRecv, channelId, algorithmId);
}

void TaskBase::GetCameraInfo(std::vector<MsgCameraInfo>& cameraInfos) {
    channel_mng_.GetCameraInfo(cameraInfos);
}

}  // namespace cosmo
