#include "util/NativeCascadeScenePolicy.h"
// TaskBaseLifecycle.cc — Lifecycle management for TaskBase.
// Split from TaskBase.cc to reduce file size (DEBT-007).

#include <algorithm>

#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskFaceAlarm.h"
#include "flow/task/TaskBase.h"
#include "util/Log.h"
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
#include "util/SleepParameterAdmission.h"
#endif
#include "util/dto/ActionCodes.h"
#ifdef COSMO_NN_USE_AXERA_BACKEND
#include "flow/detect/AiDetector.h"
#endif
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
#include "nn/device/axera/axera_face_diagnostics.h"
#endif

namespace cosmo {

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
namespace {
    bool ReleaseNativeReservation(const TaskElementPtr& task) {
        if (!task->native_face_reservation)
            return true;
        const auto channel = task->native_face_channel.lock();
        if (!channel)
            return false;
        // Stop() alone does not join demux/decoder. Quit refuses while any task
        // is active; Release verifies the matching token and clean closure.
        channel->Quit();
        if (!channel->ReleaseNativeFace(task->native_face_reservation))
            return false;
        task->native_face_reservation.reset();
        task->native_face_channel.reset();
        return true;
    }
}  // namespace
#endif

bool TaskBase::TaskRegist(TaskElementPtr task) {
    // Register task queues with parent actions
    for (auto& taNode : task->actions) {
        if (taNode.fatherAction) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
            // The camera-owned ChannelTask has only a self-linked channel node.
            // Its strict-channel role is bookkeeping; do not register any frame queue.
            const auto channel = std::dynamic_pointer_cast<AlgChannel>(taNode.actionInst);
            if (task->actions.size() == 1 && task->action_alg && task->action_alg->workFlow.empty() &&
                channel && taNode.fatherAction == taNode.actionInst &&
                channel->IsReservedCameraMetadataTask(task->taskId)) {
                task->native_camera_metadata_only = true;
                continue;
            }
#endif
            AlgTaskUnit param;
            param.channel_id = task->channelId;
            param.task_id    = task->taskId;
            //	param.actionId = taNode.action.atomicCode;
            param.actionId     = taNode.action.actionId;
            param.flowActionId = taNode.action.flowActionId;
            param.fps          = taNode.action.initFps;
            param.que          = taNode.actionInst ? taNode.actionInst->GetQueue() : nullptr;
#ifdef COSMO_NN_USE_AXERA_BACKEND
            if (const auto detector = std::dynamic_pointer_cast<AiDetector>(taNode.actionInst))
                param.native_face_registration = detector->NativeFaceRegistrationForQueue();
#endif
            if (taNode.actionInst) {
                LOG_INFO("[{}-{} Create {}] Action: {} Regist To {}, Fps:{}", task->channelId, task->taskId,
                         task->GetAlgName(), taNode.action.actionId, taNode.fatherAction->GetName(),
                         param.fps);
                if (!taNode.fatherAction->RegistTaskQueue(param)) {
                    LOG_ERRO("[{}-{} Create {}] Action: {} Regist To {} Failed", task->channelId,
                             task->taskId, task->GetAlgName(), taNode.action.actionId,
                             taNode.fatherAction->GetName());
                    return false;
                }
            } else {
                LOG_ERRO("[{}-{} Create {}] Action: {} Regist To {} Failed", task->channelId, task->taskId,
                         task->GetAlgName(), taNode.action.actionId, taNode.fatherAction->GetName());
                return false;
            }
        } else {
            task->flowActionId = taNode.action.flowActionId;
            LOG_INFO("[{}-{} Create {}] Action: {}  is Root, flowActionId is :{}", task->channelId,
                     task->taskId, task->GetAlgName(), taNode.action.actionId, taNode.action.flowActionId);
        }
    }
    return true;
}

void TaskBase::TaskUnRegist(TaskElementPtr task) {
    // Unregister task queues from parent actions
    for (auto& taNode : task->actions) {
        if (taNode.fatherAction) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
            if (task->native_camera_metadata_only && taNode.fatherAction == taNode.actionInst) {
                task->native_camera_metadata_only = false;
                continue;
            }
#endif
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
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (task->native_face_reservation &&
        (task->is_started.load(std::memory_order_acquire) || !ReleaseNativeReservation(task))) {
        LOG_WARN("[{}-{}] Native face channel reservation is still active", task->channelId, task->taskId);
        return false;
    }
#endif

#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    const bool helmet_task_drained  = !task->helmet_task_gate || task->helmet_task_gate->Cancel();
    const bool helmet_model_drained = !task->helmet_model_gate || task->helmet_model_gate->Cancel();
    if (!helmet_task_drained || !helmet_model_drained)
        return false;
#endif

    task->deletion_pending.store(true, std::memory_order_release);
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
                    "[{}-{} Remove {}] Action destroy returned false; retaining task for retry. ID:{} "
                    "ActionName:{} use_count:{}",
                    task->channelId, task->taskId, task->GetAlgName(), taNode.action.actionId,
                    actionInst->GetName(), taNode.actionInst.use_count());
                return false;
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

void TaskBase::QueueStatus(std::vector<AlgActionDataQueueStatus>& queStatus, unsigned int durationSec) {
    ForEachManager([&](auto& manager) { manager.QueueStatus(queStatus, durationSec); });
}

void TaskBase::ActionInfo(std::vector<ActionRuntimeInfo>& actionInfos) {
    ForEachManager([&](auto& manager) { manager.ActionInfo(actionInfos); });
}

bool TaskBase::TaskStart(TaskElementPtr task) {
    if (!task) {
        LOG_ERRO("{}", "Start Task Failed. Task Is Empty");
        return false;
    }
    if (task->deletion_pending.load(std::memory_order_acquire)) {
        LOG_WARN("[{}-{}] Start denied while graph deletion is pending", task->channelId, task->taskId);
        return false;
    }
    if (task->is_started.load(std::memory_order_acquire)) {
        LOG_INFO("[{}-{} {}] Alread Started", task->channelId, task->taskId, task->GetAlgName());
        return true;
    }

#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
    if (task->GetAlgId() == "65" && !sleep_candidate::SleepConfidencePatch(task->params.params, true))
        return false;
#endif

    // Validate the whole graph before changing any active-task counts.  Failing
    // halfway through the loop would otherwise leave already visited actions
    // running while the task still reports "stopped".
    for (const auto& taNode : task->actions) {
        if (!taNode.actionInst) {
            LOG_ERRO("[{}-{} {}] Start failed: action {} is not initialized", task->channelId, task->taskId,
                     task->GetAlgName(), taNode.action.actionId);
            return false;
        }
    }

#ifdef COSMO_NN_USE_AXERA_BACKEND
    bool native_selected = false;
    for (const auto& node : task->actions) {
        const auto detector = std::dynamic_pointer_cast<AiDetector>(node.actionInst);
        if (!detector)
            continue;
        const auto selection = detector->NativeFaceSelection(&node.action);
        if (selection == AiDetector::NativeSelection::Invalid ||
            (selection == AiDetector::NativeSelection::Selected && native_selected))
            return false;
        native_selected = native_selected || selection == AiDetector::NativeSelection::Selected;
    }
    if (native_selected) {
#ifdef COSMO_MEDIA_USE_AXERA_BACKEND
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        nn::axera::diagnostics::Trace(nn::axera::diagnostics::Event::TaskStartBaseline,
                                      {task->channelId, 0, 0, 0});
#endif
        if (task->native_face_reservation)
            return false;
        const auto channel = GetChannelInst(task->channelId);
        if (!channel)
            return false;
        auto token = std::make_shared<int>(0);
        // Channel owns the atomic task-set/identity check under its lock.
        if (!channel->ReserveNativeFace(token, task->taskId))
            return false;
        task->native_face_reservation = std::move(token);
        task->native_face_channel     = channel;
#else
        // Strict AX inference cannot use a different media decoder.
        return false;
#endif
    }
#endif

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND) &&                         \
    defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
    const bool native_cascade_scene = native_cascade::ProtectedScene(task->GetAlgId());
    if ((task->GetAlgId() == "88111" || task->GetAlgId() == "88113" || task->GetAlgId() == "88114" ||
         task->GetAlgId() == "88115" || task->GetAlgId() == "88116" ||
         (task->GetAlgId() == "88118" || task->GetAlgId() == "88120" || task->GetAlgId() == "58" ||
          task->GetAlgId() == "32") ||
         task->GetAlgId() == "88119") &&
        (!native_cascade_scene || !native_selected)) {
        if (!ReleaseNativeReservation(task))
            LOG_WARN("[{}-{}] Native reservation retained after electric admission failure", task->channelId,
                     task->taskId);
        return false;
    }
    if (native_cascade_scene && (!native_selected || !ConfigureNativeHelmetTask(task))) {
        LOG_WARN("ScenarioBench cascade admission failed scene={} selected={}", task->GetAlgId(),
                 native_selected);
        if (!ReleaseNativeReservation(task))
            LOG_WARN("[{}-{}] Native reservation retained after helmet admission failure", task->channelId,
                     task->taskId);
        ClearNativeHelmetTask(task);
        return false;
    }
#endif

    // In the strict route, the channel must not start reading a short video
    // before model admission and every result queue are ready.
    std::vector<size_t> started_actions;
    size_t delayed_channel = task->actions.size();
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_selected) {
        const auto channel = task->native_face_channel.lock();
        for (size_t index = 0; index < task->actions.size(); ++index) {
            if (task->actions[index].actionInst == channel) {
                if (delayed_channel != task->actions.size()) {
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
                    ClearNativeHelmetTask(task);
#endif
                    ReleaseNativeReservation(task);
                    return false;
                }
                delayed_channel = index;
            }
        }
        if (delayed_channel == task->actions.size()) {
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
            ClearNativeHelmetTask(task);
#endif
            ReleaseNativeReservation(task);
            return false;
        }
    }
#endif
    auto rollback = [&]() {
        for (auto it = started_actions.rbegin(); it != started_actions.rend(); ++it) {
            auto& node = task->actions[*it];
            node.actionInst->RemoveActiveTask();
            if (node.actionInst->GetActiveTaskCount() <= 0)
                node.actionInst->Stop();
        }
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND) &&                         \
    defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
        ClearNativeHelmetTask(task);
#endif
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (!ReleaseNativeReservation(task))
            LOG_WARN("[{}-{}] Native face reservation retained after rollback", task->channelId,
                     task->taskId);
#endif
    };
    auto start_action = [&](size_t index) {
        auto& node = task->actions[index];
        node.actionInst->AddActiveTask();
        if (!node.actionInst->Start()) {
            node.actionInst->RemoveActiveTask();
            return false;
        }
        started_actions.push_back(index);
        return true;
    };
    for (size_t index = 0; index < task->actions.size(); ++index) {
        if (index == delayed_channel)
            continue;
        if (!start_action(index)) {
            rollback();
            return false;
        }
    }
#ifdef COSMO_NN_USE_AXERA_BACKEND
    for (const auto& node : task->actions) {
        const auto detector = std::dynamic_pointer_cast<AiDetector>(node.actionInst);
        if (!detector)
            continue;
        const auto selection = detector->NativeFaceSelection(&node.action);
        const auto native = detector->PrepareProvisionedNativeFace(task->GetAlgId(), task->action_alg.get());
        if ((selection == AiDetector::NativeSelection::Selected &&
             native != AiDetector::NativeProvisionStatus::Ready) ||
            (selection == AiDetector::NativeSelection::NotSelected &&
             native != AiDetector::NativeProvisionStatus::NotConfigured) ||
            selection == AiDetector::NativeSelection::Invalid) {
            LOG_WARN("[{}-{}] Native face task is not admitted", task->channelId, task->taskId);
            rollback();
            return false;
        }
    }
#endif
    if (!TaskRegist(task)) {
        TaskUnRegist(task);
        rollback();
        return false;
    }
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND) &&                         \
    defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
    if (!InstallNativeHelmetTask(task)) {
        TaskUnRegist(task);
        rollback();
        return false;
    }
#endif
    if (delayed_channel != task->actions.size() && !start_action(delayed_channel)) {
        TaskUnRegist(task);
        rollback();
        return false;
    }
    task->is_started.store(true, std::memory_order_release);
    return true;
}

bool TaskBase::TaskStop(TaskElementPtr task) {
    if (!task) {
        LOG_ERRO("{}", "Stop Task Failed. Task Is Empty");
        return false;
    }
#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    // Both cancellation requests happen before any queue unregister or action
    // refcount change. A false return leaves the graph intact for retry.
    const bool helmet_task_drained  = !task->helmet_task_gate || task->helmet_task_gate->Cancel();
    const bool helmet_model_drained = !task->helmet_model_gate || task->helmet_model_gate->Cancel();
    if (!helmet_task_drained || !helmet_model_drained)
        return false;
#endif
#if defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    const bool native_stop = static_cast<bool>(task->native_face_reservation);
#endif
    if (!task->is_started.load(std::memory_order_acquire)) {
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (!ReleaseNativeReservation(task))
            return false;
#endif
#if defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
        if (native_stop)
            nn::axera::diagnostics::Trace(nn::axera::diagnostics::Event::TaskStopComplete,
                                          {task->channelId, 0, 0, 0});
#endif
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND) &&                         \
    defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
        ClearNativeHelmetTask(task);
#endif
        LOG_INFO("[{}-{} {}] Not started, skip stop", task->channelId, task->taskId, task->GetAlgName());
        return true;
    }
    TaskUnRegist(task);
    for (auto& taNode : task->actions) {
        if (!taNode.actionInst) {
            LOG_WARN("[{}-{} {}] Stop skipped for uninitialized action {}", task->channelId, task->taskId,
                     task->GetAlgName(), taNode.action.actionId);
            continue;
        }
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
    task->is_started.store(false, std::memory_order_release);
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (!ReleaseNativeReservation(task)) {
        LOG_WARN("[{}-{}] Native face reservation retained after stop", task->channelId, task->taskId);
        return false;
    }
#endif
#if defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) && defined(COSMO_MEDIA_USE_AXERA_BACKEND)
    if (native_stop)
        nn::axera::diagnostics::Trace(nn::axera::diagnostics::Event::TaskStopComplete,
                                      {task->channelId, 0, 0, 0});
#endif
#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_MEDIA_USE_AXERA_BACKEND) &&                         \
    defined(COSMO_AXERA_HELMET_ROUTE_CANDIDATE)
    ClearNativeHelmetTask(task);
#endif
    return true;
}

bool TaskBase::TaskIsStart(TaskElementPtr task) {
    return task && task->is_started.load(std::memory_order_acquire);
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

#ifdef COSMO_AXERA_HELMET_ROUTE_CANDIDATE
    // This disabled overlay does not yet implement atomic configuration-generation
    // rotation. Refuse mutation while a candidate gate is attached; do not allow
    // old results to commit under new thresholds. Existing tasks are unaffected.
    if (task->helmet_task_gate || task->helmet_model_gate) {
        LOG_WARN("[{}/{}] Helmet candidate config rotation is not implemented", task->channelId,
                 task->taskId);
        return false;
    }
#endif

#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
    if (task->GetAlgId() == "65") {
        if (!sleep_candidate::SleepConfidencePatch(taskConfig.params, true))
            return false;
        task->params.params.clear();
    }
#endif

    // Freeze detector identities from the whole service graph before applying parameters.
    // This includes unsupported detection stages: their missing provenance must suppress
    // private images, including when another branch has already produced an alarm.
    std::vector<std::string> privacyDetectors;
    for (const auto& node : task->actions) {
        if (!node.actionInst) {
            LOG_ERRO("[{}/{}] Cannot configure privacy topology: action is uninitialized", task->channelId,
                     task->taskId);
            return false;
        }
        switch (node.actionInst->GetActionType()) {
            case AlgActionType::AlgActionAiDetect:
            case AlgActionType::AlgActionDinoDetect:
            case AlgActionType::AlgActionSam2Segment:
            case AlgActionType::AlgActionAiPersonFace:
            case AlgActionType::AlgActionAiOcr: {
                const auto id = node.actionInst->GetUuid();
                if (std::find(privacyDetectors.begin(), privacyDetectors.end(), id) ==
                    privacyDetectors.end()) {
                    privacyDetectors.push_back(id);
                }
                break;
            }
            default:
                break;
        }
    }
    for (const auto& node : task->actions) {
        if (auto alarm = std::dynamic_pointer_cast<TaskAlarm>(node.actionInst)) {
            alarm->ConfigurePrivacyDetectors(privacyDetectors);
        } else if (auto faceAlarm = std::dynamic_pointer_cast<TaskFaceAlarm>(node.actionInst)) {
            faceAlarm->ConfigurePrivacyDetectors(privacyDetectors);
        }
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

#ifdef COSMO_AXERA_PHONE_ROUTE_CANDIDATE
    if (task->GetAlgId() == "11" || task->GetAlgId() == "12" || task->GetAlgId() == "22" ||
        task->GetAlgId() == "88110" || task->GetAlgId() == "88111" || task->GetAlgId() == "88112" ||
        task->GetAlgId() == "88113" || task->GetAlgId() == "88114" || task->GetAlgId() == "88115" ||
        task->GetAlgId() == "88116" || task->GetAlgId() == "88117" ||
        (task->GetAlgId() == "88118" || task->GetAlgId() == "88120" || task->GetAlgId() == "58" ||
         task->GetAlgId() == "32") ||
        task->GetAlgId() == "88119")
        task->params = taskConfig;
#endif
#ifdef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
    if (task->GetAlgId() == "65")
        task->params = taskConfig;
#endif
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
