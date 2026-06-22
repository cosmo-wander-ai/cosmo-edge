// Stream-analysis task base — task creation, action handler registration.

#include "flow/task/TaskBase.h"

#include "util/Keys.h"
#include "util/Log.h"
#include "util/UuidUtil.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

TaskBase::TaskBase() {
    RegisterActionHandlers();
    RegisterMngProviders();
    LOG_INFO("TaskBase Init, {} action handlers registered, {} mng providers", action_handlers_.size(),
             mng_providers_.size());
}

TaskBase::~TaskBase() {
    LOG_INFO("{}", "TaskBase Delete");
}

// Generic handler factory for Mng types with GetInst(taskId, action) / DeleteInst(inst) signatures
template <typename MngT, typename InstT>
static ActionHandler MakeHandler(MngT& mng) {
    return {[&mng](const std::string&, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
                return mng.GetInst(tId, action);
            },
            [&mng](AlgActionBasePtr inst, const std::string&, const std::string&) -> bool {
                return mng.DeleteInst(std::dynamic_pointer_cast<InstT>(inst));
            }};
}

void TaskBase::RegisterActionHandlers() {
    // --- AA (Atomic Algorithm) Actions ---

    // Detection (special: requires algCode + channelId)
    action_handlers_[AADetect_Code] = {
        [this](const std::string& chId, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return detect_mng_.GetInst(action.atomAlgName, chId, tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string& chId, const std::string& tId) -> bool {
            return detect_mng_.DeleteInst(std::dynamic_pointer_cast<AiDetector>(inst), chId, tId);
        }};

    // Dino detection visual large model (similar to detection: algCode + channelId + taskId)
    action_handlers_[DADinoDetect_Code] = {
        [this](const std::string& chId, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return dino_detect_mng_.GetInst(
                action.atomicCode.empty() ? action.atomAlgName : action.atomicCode, chId, tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string& chId, const std::string& tId) -> bool {
            return dino_detect_mng_.DeleteInst(std::dynamic_pointer_cast<DinoDetector>(inst), chId, tId);
        }};

    // Qwen3VL visual language large model (similar to Dino: algCode + channelId + taskId)
    action_handlers_[DAQwen3VL_Code] = {
        [this](const std::string& chId, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return qwen3_vl_mng_.GetInst(action.atomicCode.empty() ? action.atomAlgName : action.atomicCode,
                                         chId, tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string& chId, const std::string& tId) -> bool {
            return qwen3_vl_mng_.DeleteInst(std::dynamic_pointer_cast<Qwen3VLWorker>(inst), chId, tId);
        }};

    // Sam2 segmentation large model (similar to Dino: algCode + channelId + taskId)
    action_handlers_[DASam2Segment_Code] = {
        [this](const std::string& chId, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return sam2_segment_mng_.GetInst(
                action.atomicCode.empty() ? action.atomAlgName : action.atomicCode, chId, tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string& chId, const std::string& tId) -> bool {
            return sam2_segment_mng_.DeleteInst(std::dynamic_pointer_cast<Sam2Segmenter>(inst), chId, tId);
        }};

    // Tracking (special: DeleteInst requires taskId)
    action_handlers_[AATrack_Code] = {
        [this](const std::string&, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return track_mng_.GetInst(tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string&, const std::string& tId) -> bool {
            return track_mng_.DeleteInst(std::dynamic_pointer_cast<AiTracker>(inst), tId);
        }};

    action_handlers_[AAClassify_Code] = MakeHandler<AiClassifyMng, AiClassifier>(classify_mng_);
    action_handlers_[AAClassifyGroup_Code] =
        MakeHandler<AiClassifyGroupMng, AiClassifierGroup>(classify_group_mng_);
    action_handlers_[AAClassifyArea_Code] =
        MakeHandler<AiClassifyAreaMng, AiClassifierArea>(classify_area_mng_);
    action_handlers_[AAClassifyAttr_Code] =
        MakeHandler<AiClassifyAttrMng, AiClassifierAttr>(classify_attr_mng_);
    action_handlers_[AALandmark_Code] = MakeHandler<AiLandmarkMng, AiLandmark>(landmark_mng_);

    // Video diagnosis (special: DeleteInst requires taskId)
    action_handlers_[AAVideoDiagnosis_Code] = {
        [this](const std::string&, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return ai_video_quality_mng_.GetInst(tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string&, const std::string& tId) -> bool {
            return ai_video_quality_mng_.DeleteInst(std::dynamic_pointer_cast<AiVideoQuality>(inst), tId);
        }};

    // --- BA (Business Algorithm) Actions ---

    action_handlers_[BAFilter_Code] = MakeHandler<TargetFilterMng, TargetFilter>(filter_mng_);
    action_handlers_[BALogicalJudgment_Code] =
        MakeHandler<LogicalJudgmentMng, LogicalJudgment>(logical_judgment_mng_);
    action_handlers_[BAActionBranch_Code] = MakeHandler<ActionBranchMng, ActionBranch>(action_branch_mng_);
    action_handlers_[BATargetChooseBest_Code] =
        MakeHandler<TargetChooseBestMng, TargetChooseBest>(target_choose_best_mng_);
    action_handlers_[BAAreaAlarm_Code] = MakeHandler<AreaAlarmMng, AreaAlarm>(area_alarm_mng_);
    action_handlers_[BAPositiveSaveSensitivity_Code] =
        MakeHandler<PosSaveSensitivityMng, PosSaveSensitivity>(pos_save_sensitivity_mng_);

    // Sensitivity (two codes share the same handler)
    auto sensitivityHandler                      = MakeHandler<SensitivityMng, Sensitivity>(sensitivity_mng_);
    action_handlers_[BASensitivity_Code]         = sensitivityHandler;
    action_handlers_[BAFixCountSensitivity_Code] = sensitivityHandler;

    // Alarm (special: GetInst requires channelId)
    action_handlers_[BATaskAlarm_Code] = {
        [this](const std::string& chId, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return task_alarm_mng_.GetInst(chId, tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string&, const std::string&) -> bool {
            return task_alarm_mng_.DeleteInst(std::dynamic_pointer_cast<TaskAlarm>(inst));
        }};

    // Face snapshot logic
    action_handlers_[BAFaceLogic_Code] = {
        [this](const std::string&, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return face_logic_mng_.GetInst(tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string&, const std::string&) -> bool {
            return face_logic_mng_.DeleteInst(std::dynamic_pointer_cast<FaceLogic>(inst));
        }};

    // Behavior-based face comparison alarm
    action_handlers_[BATaskFaceAlarm_Code] = {
        [this](const std::string& chId, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            // Get algId/algName from action_alg
            auto algId   = action.atomicCode;
            auto algName = action.actionName;
            return task_face_alarm_mng_.GetInst(chId, tId, algId, algName, action);
        },
        [this](AlgActionBasePtr inst, const std::string&, const std::string&) -> bool {
            return task_face_alarm_mng_.DeleteInst(std::dynamic_pointer_cast<TaskFaceAlarm>(inst));
        }};

    // Feature extraction / recognition
    action_handlers_[AARecognizer_Code] = {
        [this](const std::string&, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            // Fallback: use atomAlgName when atomicCode is not parsed/filled in orchestration
            const std::string algCode = action.atomicCode.empty() ? action.atomAlgName : action.atomicCode;
            return recognizer_mng_.GetInst(tId, algCode, action);
        },
        [this](AlgActionBasePtr inst, const std::string&, const std::string& tId) -> bool {
            auto recogInst = std::dynamic_pointer_cast<AiRecognizer>(inst);
            return recognizer_mng_.DeleteInst(recogInst, tId, recogInst ? recogInst->GetName() : "");
        }};

    // Stream channel (special: GetInst/DeleteInst require channelId/taskId)
    action_handlers_[BAStreamChannel_Code] = {
        [this](const std::string& chId, const std::string& tId, ActionNode& action) -> AlgActionBasePtr {
            return channel_mng_.GetInst(chId, tId, action);
        },
        [this](AlgActionBasePtr inst, const std::string&, const std::string& tId) -> bool {
            return channel_mng_.DeleteInst(std::dynamic_pointer_cast<AlgChannel>(inst), tId);
        }};
}

TaskElementPtr TaskBase::TaskCreate(const std::string& channelId, const std::string& channelName,
                                    const std::string& taskId, ActionAlgPtr actionAlg) {
    // Orchestration config is empty
    if (!actionAlg) {
        LOG_ERRO("Create Task [{}/{}] Failed. But The ActionAlg Is Empty", channelId, taskId);
        return nullptr;
    }

    TaskElementPtr task = std::make_shared<TaskElement>();

    task->channelId   = channelId;
    task->channelName = channelName;
    task->taskId      = taskId;
    task->action_alg  = actionAlg;

    bool bHaveChanelAction = false;
    for (auto actionNode : actionAlg->workFlow) {
        TaskAction ta;
        for (auto& actionKeyParam : actionNode.configObject.params) {
            auto keys = util::Split(actionKeyParam.key.ToRefString(), ".");
            actionKeyParam.keys.assign(keys.begin(), keys.end());
        }

        ta.action = actionNode;

        // BAStreamChannel special handling: override actionId/actionName
        if (0 == BAStreamChannel_Code.compare(actionNode.actionId)) {
            ta.action.actionId   = BAStreamChannel_Code;
            ta.action.actionName = BAStreamChannel_Name;
            bHaveChanelAction    = true;
        }

        auto it = action_handlers_.find(actionNode.actionId);
        if (it != action_handlers_.end()) {
            ta.actionInst = it->second.create(channelId, taskId, actionNode);
        } else {
            LOG_WARN("[{}-{} Create {}] Action: {}-{} Not Support", channelId, taskId,
                     actionAlg->algorithmName, actionNode.actionId, actionNode.actionName);
            return nullptr;
        }

        LOG_INFO("[{}-{} Create {}] Action Add: {} ", channelId, taskId, actionAlg->algorithmName,
                 ta.action.actionId);
        if (ta.actionInst) {
            ta.actionInst->RegisterTaskContext(taskId, actionAlg, ta.action);
        }
        task->actions.push_back(ta);
    }

    // Note: when no channel action is present, handle reverse registration accordingly
    if (!bHaveChanelAction) {
        TaskAction ta;
        ta.action.actionId   = BAStreamChannel_Code;
        ta.action.actionName = BAStreamChannel_Name;
        auto channelInst     = channel_mng_.GetInst(channelId, taskId, ta.action);
        ta.actionInst        = channelInst;
        task->actions.push_back(ta);
    }

    // Root node (BAStreamChannel) with flowActionId "-1" prevents child nodes from matching
    // preFlowActionId, so Decode won't register queues with AiDetector and RGB video won't
    // enter algorithm analysis (only preview works). Assign a valid flowActionId to root
    // (consistent with old d187506b).
    std::string rootFlowIdReplace;
    for (auto& taNode : task->actions) {
        if (0 == BAStreamChannel_Code.compare(taNode.action.actionId) &&
            taNode.action.flowActionId == key::alg::ACTION_ROOT_VALUE) {
            rootFlowIdReplace          = util::GenerateUUID();
            taNode.action.flowActionId = rootFlowIdReplace;
            LOG_INFO("[{}-{} Create {}] Action: {} flowActionId -1 replaced with {} for father link",
                     channelId, taskId, actionAlg->algorithmName, taNode.action.actionId, rootFlowIdReplace);
            break;
        }
    }
    if (!rootFlowIdReplace.empty()) {
        for (auto& taNode : task->actions) {
            if (taNode.action.preFlowActionId == key::alg::ACTION_ROOT_VALUE)
                taNode.action.preFlowActionId = rootFlowIdReplace;
        }
    }

    // Reverse registration: find parent nodes for each action
    for (auto& taNode : task->actions) {
        LOG_INFO("[{}-{} Create {}] Action: {} Search Father ActionId:{} preFlowActionId:{}", channelId,
                 taskId, actionAlg->algorithmName, taNode.action.actionId, taNode.action.flowActionId,
                 taNode.action.preFlowActionId);
        if (taNode.action.flowActionId == key::alg::ACTION_ROOT_VALUE) {
            // Root node: built-in channel node
            continue;
        }
        // Find parent node
        auto it = std::find_if(task->actions.begin(), task->actions.end(), [&](const auto& taNodePa) {
            return taNode.action.preFlowActionId == taNodePa.action.flowActionId;
        });
        if (it != task->actions.end()) {
            LOG_INFO("[{}-{} Create {}] Action: {}/{} Get Father is {}/{}", channelId, taskId,
                     actionAlg->algorithmName, taNode.action.actionId, taNode.action.actionName,
                     it->action.actionId, it->action.actionName);
            taNode.fatherAction = it->actionInst;
        } else {
            LOG_WARN("[{}-{} Create {}] Action: {} Have No Father preFlowActionId:{}", channelId, taskId,
                     actionAlg->algorithmName, taNode.action.actionId, taNode.action.preFlowActionId);
        }
    }

    LOG_INFO("[{}-{} Create {}] Ok", channelId, taskId, actionAlg->algorithmName);
    return task;
}

// Lifecycle management — moved to TaskBaseLifecycle.cc

}  // namespace cosmo
