// Picture-detection task base — task creation, deletion, action init/destroy and param modification.

#include "flow/task/PTaskBase.h"

#include <algorithm>
#include <filesystem>

#include "flow/detect/PDinoDetector.h"
#include "flow/detect/PSamDetector.h"
#include "flow/landmark/PLandmark.h"
#include "flow/qwen3vl/PQwen3VLWorker.h"
#include "flow/recognizer/PRecognizer.h"
#include "media/Color.h"
#include "service/detail/ServiceRegistry.h"
#include "service/path/IFileService.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/dto/ActionCodes.h"

namespace cosmo {

PTaskBase::PTaskBase() {
    LOG_INFO("{}", "PTaskBase Init");
}

PTaskBase::~PTaskBase() {
    LOG_INFO("{}", "PTaskBase Delete");
}

PTaskElementPtr PTaskBase::TaskCreate(const std::string& taskId, ActionAlgPtr actionAlg) {
    // Orchestration config is empty
    if (!actionAlg) {
        LOG_ERRO("Create Task [{}] Failed. But The ActionAlg Is Empty", taskId);
        return nullptr;
    }

    PTaskElementPtr task = std::make_shared<PTaskElement>();

    task->taskId     = taskId;
    task->action_alg = actionAlg;

    for (auto actionNode : actionAlg->workFlow) {
        for (auto& actionKeyParam : actionNode.configObject.params) {
            auto keys = util::Split(actionKeyParam.key.ToRefString(), ".");
            actionKeyParam.keys.assign(keys.begin(), keys.end());
        }

        PTaskAction ta;
        ta.action = actionNode;
        if (0 == PADetect_Code.compare(actionNode.actionId)) {
            // Detection instance
            auto detectorInst = m_detectorMng.GetInst(taskId, actionNode);
            ta.actionInst     = detectorInst;
        } else if (0 == PAClassify_Code.compare(actionNode.actionId)) {
            // Classification instance
            auto detectorInst = m_classifierMng.GetInst(taskId, actionNode);
            ta.actionInst     = detectorInst;
        } else if (0 == PALandmark_Code.compare(actionNode.actionId)) {
            // Landmark instance
            auto landmarkInst = m_landmarkMng.GetInst(taskId, actionNode);
            ta.actionInst     = landmarkInst;
        } else if (0 == PARecognizer_Code.compare(actionNode.actionId)) {
            // Feature extraction instance
            auto recognizerInst = m_recognizerMng.GetInst(taskId, actionNode);
            ta.actionInst       = recognizerInst;
        } else if (0 == PDADino_Code.compare(actionNode.actionId)) {
            ta.actionInst = std::make_shared<PDinoDetector>(actionNode, taskId);
        } else if (0 == PDASam_Code.compare(actionNode.actionId)) {
            ta.actionInst = std::make_shared<PSamDetector>(actionNode, taskId);
        } else if (0 == PDAQwen3VL_Code.compare(actionNode.actionId)) {
            ta.actionInst = std::make_shared<PQwen3VLWorker>(actionNode, taskId);
        } else if (0 == PALogicalJudgment_Code.compare(actionNode.actionId)) {
            // Logical judgment instance
            auto detectorInst = m_logicJudgmentMng.GetInst(taskId, actionNode);
            ta.actionInst     = detectorInst;
        } else {
            LOG_WARN("[{} Create {}] Action: {}-{} Not Support", taskId, actionAlg->algorithmName,
                     actionNode.actionId, actionNode.actionName);
            // Release nodes on unsupported action?
            return nullptr;
        }
        LOG_INFO("[{} Create {}] Action Add: {} ", taskId, actionAlg->algorithmName, ta.action.actionId);
        task->actions.push_back(ta);
    }

    LOG_INFO("[{} Create {}] Ok", taskId, actionAlg->algorithmName);
    return task;
}

bool PTaskBase::TaskDelete(PTaskElementPtr task) {
    // Orchestration config is empty
    if (!task) {
        LOG_ERRO("Delete Task Failed. Task Is Empty", task);
        return false;
    }

    LOG_INFO("[{} Remove {}]", task->taskId, task->GetAlgName());

    // Unregister tasks (legacy code, kept for reference via git history)

    // Release actions
    for (auto& taNode : task->actions) {
        auto actionInst = taNode.actionInst;
        if (!actionInst) {
            LOG_WARN("[{} Remove {}] Failed, ActionInst Is Empty", task->taskId, task->GetAlgName());
            return false;
        }
        bool instDelRet = false;
        auto actionId   = actionInst->GetActionId();
        if (0 == PADetect_Code.compare(actionId)) {
            instDelRet = m_detectorMng.DeleteInst(std::dynamic_pointer_cast<PDetector>(actionInst));
        } else if (0 == PAClassify_Code.compare(actionId)) {
            instDelRet = m_classifierMng.DeleteInst(std::dynamic_pointer_cast<PClassifier>(actionInst));
        } else if (0 == PALandmark_Code.compare(actionId)) {
            instDelRet = m_landmarkMng.DeleteInst(std::dynamic_pointer_cast<PLandmark>(actionInst));
        } else if (0 == PARecognizer_Code.compare(actionId)) {
            instDelRet = m_recognizerMng.DeleteInst(std::dynamic_pointer_cast<PRecognizer>(actionInst));
        } else if (0 == PALogicalJudgment_Code.compare(actionId)) {
            instDelRet =
                m_logicJudgmentMng.DeleteInst(std::dynamic_pointer_cast<PLogicalJudgment>(actionInst));
        } else if (0 == PDADino_Code.compare(actionId) || 0 == PDASam_Code.compare(actionId) ||
                   0 == PDAQwen3VL_Code.compare(actionId)) {
            instDelRet = true;  // Standalone instance, destroyed by std::shared_ptr when taNode is released
        } else {
            LOG_WARN("[{} Remove {}] Failed, Action Unknow :ID:{} ActionName:{}", task->taskId,
                     task->GetAlgName(), taNode.action.actionId, actionInst->GetName());
            return false;
        }

        if (instDelRet) {
            taNode.actionInst.reset();
        }

        LOG_INFO("[{} Remove {}] Action ID:{} ", task->taskId, task->GetAlgName(), taNode.action.actionId);
    }
    LOG_INFO("[{} Remove {}] Ok ", task->taskId, task->GetAlgName());
    return true;
}

bool PTaskBase::TaskActionInit(PTaskElementPtr task) {
    if (task->is_started) {
        LOG_INFO("[{} {}] Alread Started", task->taskId, task->GetAlgName());
        return true;
    }
    task->startFailedCount += 1;
    for (auto& taNode : task->actions) {
        if (taNode.actionInst->ActionInit()) {
            LOG_INFO("[{} {}] Action {} {} Start", task->taskId, task->GetAlgName(), taNode.action.actionId,
                     taNode.action.actionName);
        } else {
            task->errorInfo = taNode.action.actionId + " " + taNode.action.flowActionId + " Start Failed";
            return false;
        }
    }
    task->is_started       = true;
    task->startFailedCount = 0;
    return true;
}

bool PTaskBase::TaskActionDestroy(PTaskElementPtr task) {
    for (auto& taNode : task->actions) {
        LOG_DEBUG("[{} {}] {} root:{} task:{}", task->taskId, task->GetAlgName(), taNode.action.actionName,
                  task->flowActionId, taNode.action.flowActionId);
        // if (task->flowActionId == taNode.action.flowActionId)
        {
            LOG_INFO("[{} {}] Stop {}", task->taskId, task->GetAlgName(), taNode.action.actionName);
            taNode.actionInst->ActionDestroy();
        }
    }
    task->is_started = false;
    return true;
}

bool PTaskBase::ModifyTaskParam(PTaskElementPtr task, MsgTaskConfig& taskConfig) {
    // Orchestration config is empty
    if (!task) {
        LOG_ERRO("{}", "Task Is Empty");
        return false;
    }

    // Key: ModifyParam relies on param.keys (e.g. param.faceSet)
    for (auto& kv : taskConfig.params) {
        if (kv.keys.empty() && !kv.key.empty()) {
            auto parts = util::Split(kv.key.ToRefString(), ".");
            kv.keys.assign(parts.begin(), parts.end());
        }
    }

    for (auto& taNode : task->actions) {
        LOG_INFO("[{} {}] ModifyParam For {}/{} params.size:{} areas.size:{} shieldedAreas.size:{}",
                 task->taskId, task->GetAlgName(), taNode.action.actionId, taNode.action.actionName,
                 taskConfig.params.size(), taskConfig.areas.size(), taskConfig.shieldedAreas.size());
        taNode.actionInst->ModifyParam(task->taskId, taskConfig.params);
        taNode.actionInst->SetArea(task->taskId, taskConfig.areas, taskConfig.shieldedAreas);
    }

    return true;
}

// DetTarget2MsgTarget and image upload — in PTaskBaseUpload.cc

}  // namespace cosmo
