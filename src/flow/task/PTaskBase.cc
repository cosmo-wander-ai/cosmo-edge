// Picture-detection task base — task creation, deletion, action init/destroy and param modification.

#include "flow/task/PTaskBase.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "flow/detect/PDinoDetector.h"
#include "flow/detect/PSamDetector.h"
#include "flow/landmark/PLandmark.h"
#include "flow/logical/PictureRule.h"
#include "flow/ocr/POcr.h"
#include "flow/qwen3vl/PQwen3VLWorker.h"
#include "flow/recognizer/PPictureMatch.h"
#include "flow/recognizer/PRecognizer.h"
#include "media/Color.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/path/IFileService.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/dto/ActionCodes.h"
#include "util/dto/PictureWorkflow.h"

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

    auto workflow = actionAlg->workFlow;
    if (!ValidatePictureWorkflow(workflow, task->errorInfo)) {
        LOG_WARN("Invalid picture workflow: {}", task->errorInfo);
        return nullptr;
    }
    std::map<std::string, std::string> featureModels;
    for (auto actionNode : workflow) {
        for (const auto& param : actionNode.configObject.params)
            if (param.key == "atomicCode")
                actionNode.atomicCode = param.value.ToString();
        auto featureModel = featureModels[actionNode.preFlowActionId];
        if (actionNode.actionId == PARecognizer_Code)
            featureModel = actionNode.atomicCode;
        featureModels[actionNode.flowActionId] = featureModel;
        if (actionNode.actionId == PAMatch_Code && actionNode.atomicCode.empty())
            actionNode.atomicCode = featureModel;
        for (auto& param : actionNode.configObject.params) {
            auto keys = util::Split(param.key.ToRefString(), ".");
            param.keys.assign(keys.begin(), keys.end());
        }
        PTaskAction ta;
        ta.action      = actionNode;
        const auto& id = actionNode.actionId;
        if (id == PADetect_Code)
            ta.actionInst = std::make_shared<PDetector>(taskId, actionNode);
        else if (id == PAClassify_Code)
            ta.actionInst = std::make_shared<PClassifier>(taskId, actionNode);
        else if (id == PALandmark_Code)
            ta.actionInst = std::make_shared<PLandmark>(taskId, actionNode);
        else if (id == PARecognizer_Code)
            ta.actionInst = std::make_shared<PRecognizer>(taskId, actionNode);
        else if (id == PAOcr_Code)
            ta.actionInst = std::make_shared<POcr>(taskId, actionNode);
        else if (id == PAMatch_Code)
            ta.actionInst = std::make_shared<PPictureMatch>(taskId, actionNode);
        else if (id == PDADino_Code)
            ta.actionInst = std::make_shared<PDinoDetector>(actionNode, taskId);
        else if (id == PDASam_Code)
            ta.actionInst = std::make_shared<PSamDetector>(actionNode, taskId);
        else if (id == PDAQwen3VL_Code)
            ta.actionInst = std::make_shared<PQwen3VLWorker>(actionNode, taskId);
        else
            ta.actionInst = std::make_shared<PictureRuleAction>(taskId, actionNode);
        if (!ta.actionInst->SetParam(taskId, actionNode.configObject.params))
            return nullptr;
        task->actions.push_back(std::move(ta));
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

    task->actions.clear();
    LOG_INFO("[{} Remove {}] Ok ", task->taskId, task->GetAlgName());
    return true;
}

bool PTaskBase::TaskActionInit(PTaskElementPtr task) {
    if (task->is_started) {
        LOG_INFO("[{} {}] Alread Started", task->taskId, task->GetAlgName());
        return true;
    }
    task->startFailedCount += 1;
    std::map<std::string, std::vector<MsgDynamicKeyValue>> inherited;
    for (auto& taNode : task->actions) {
        taNode.modelParams = inherited[taNode.action.preFlowActionId];
        if (taNode.actionInst->ActionInit()) {
            if (IsPictureModelAction(taNode.action.actionId) && taNode.action.actionId != PDAQwen3VL_Code) {
                std::string config, model;
                if (service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
                        taNode.actionInst->GetAtomicCode(), config, model)) {
                    std::ifstream stream(config);
                    const auto json = nlohmann::json::parse(stream, nullptr, false);
                    if (json.is_object() && json.contains("labels") && json["labels"].is_array()) {
                        for (const auto& label : json["labels"]) {
                            if (!label.is_object() || !label.contains("name") || !label["name"].is_string() ||
                                !label.contains("threshold") || !label["threshold"].is_array() ||
                                label["threshold"].empty() || !label["threshold"][0].is_number())
                                continue;
                            MsgDynamicKeyValue value;
                            value.key   = "aiParam." + label["name"].get<std::string>() + ".confidence";
                            value.value = std::to_string(label["threshold"][0].get<double>());
                            taNode.modelParams.push_back(std::move(value));
                        }
                    }
                }
            }
            inherited[taNode.action.flowActionId] = taNode.modelParams;
            LOG_INFO("[{} {}] Action {} {} Start", task->taskId, task->GetAlgName(), taNode.action.actionId,
                     taNode.action.actionName);
        } else {
            task->errorInfo = taNode.action.actionId + " " + taNode.action.flowActionId + " Start Failed";
            for (auto& initialized : task->actions)
                initialized.actionInst->ActionDestroy();
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

    if (!taskConfig.areas.empty() || !taskConfig.shieldedAreas.empty())
        return false;
    task->params = taskConfig;

    return true;
}

// DetTarget2MsgTarget and image upload — in PTaskBaseUpload.cc

}  // namespace cosmo
