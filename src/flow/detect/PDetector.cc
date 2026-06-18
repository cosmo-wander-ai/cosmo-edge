// PDetector.cc — Per-task YOLO detector implementation for picture/snapshot mode.

#include "flow/detect/PDetector.h"

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/GeometricPos.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

namespace cosmo {

PDetector::~PDetector() {
    LOG_INFO("[{} {}] Stop", GetTaskId(), GetFlowActionId());
    if (inst_) {
        inst_.reset();
        inst_ = nullptr;
    }
    LOG_INFO("[{} {}] Delete", GetTaskId(), GetFlowActionId());
}

PDetector::PDetector(const std::string& taskId, ActionNode& action)
    : PActionBase(action, taskId),
      duration_stat_(taskId + "-" + action.actionName + "-" + action.flowActionId) {
    action_status_ = util::ErrorEnum::ActionReady;
    LOG_INFO("[{} {}] Init ", GetTaskId(), GetFlowActionId());
}

bool PDetector::ActionInit() {
    // 1. Error code  2. SDK initialization
    if (inst_) {
        LOG_INFO("[{} {}] Sdk Have Init", GetTaskId(), GetFlowActionId());
        return true;
    }

    std::string cfgPath   = "";
    std::string modelPath = "";
    auto cfgRet           = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
                  GetAtomicCode(), cfgPath, modelPath);
    if (!cfgRet) {
        LOG_WARN("Get Model Configure Failed. AlgCode:{} code:{}", GetAtomicCode(), cfgRet);
        return false;
    }
    inst_    = std::make_shared<AiDetectorUnify>(GetAtomicCode(), cfgPath, modelPath);
    auto ret = inst_->Init();
    if (util::ErrorEnum::Success != ret) {
        inst_.reset();
        inst_ = nullptr;
        LOG_WARN("[{} {}] {} Sdk Init Failed Get Ret {}", GetTaskId(), GetFlowActionId(), GetAtomicCode(),
                 ret);
        return false;
    }

    action_status_ = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("[{} {}] Init Sdk", GetTaskId(), GetFlowActionId());
    return true;
}

// Parameter key format: aiParam.#{labelCode}.confidence
bool PDetector::ValidKey(const MsgDynamicKeyValue& param) const {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{} {}] param.keys is Empty",
            GetTaskId(), GetFlowActionId());
        return false;
    }
    if (param.keys.size() != 3) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] Set {} Failed. key size:{}",
            GetTaskId(), GetFlowActionId(), param.key, param.keys.size());
        return false;
    }

    if (key::AI_PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.keys[0]:{} is Not {}",
            GetTaskId(), GetFlowActionId(), param.keys[0], key::AI_PARAM);
        return false;
    }

    return true;
}

bool PDetector::AnalysisKey(const MsgDynamicKeyValue& param) {
    if (!ValidKey(param)) {
        return false;
    }

    if (param.keys[2] == key::CONFIDENCE) {
        AiConfidence confidence;
        confidence.label      = param.keys[1];
        confidence.confidence = util::ParseFloat(param.value);
        if (confidence.confidence < 0.01f) {
            LOG_WARN(
                "ModifyParam "
                "[{} {}] param.key {} value {}",
                GetTaskId(), GetFlowActionId(), param.key, param.value);
        } else {
            params_.confidence.push_back(confidence);
            LOG_INFO(
                "ModifyParam "
                "[{} {}] param.key {} value {}",
                GetTaskId(), GetFlowActionId(), param.key, param.value);
        }
    } else {
        LOG_DEBUG(
            "ModifyParam "
            "[{} {}] param.key {} value {} is Unknown",
            GetTaskId(), GetFlowActionId(), param.key, param.value);
        return false;
    }

    return false;
}

// Modify parameters — update on top of existing parameters
bool PDetector::ModifyParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    params_.confidence = {};
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

// Set parameters — clear previous parameters, set all new parameters
bool PDetector::SetParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    // Clear existing parameters
    params_ = {};
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

// Set areas — clear previous areas, set all new areas
bool PDetector::SetArea(const std::string& taskId, std::vector<MsgTaskArea>& areas,
                        std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    task_area_.taskId        = taskId;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shieldedAreas;
    for (auto& area : task_area_.areas) {
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& assArea : area.associatedAreas) {
            assArea.iderectionType = GetDirectionTypeFromMsg(assArea.params);
        }
    }
    return true;
}

util::ErrorEnum PDetector::HandPic(AlgDataPtr algData) {
    if (!algData || !algData->chanDataDec.frame || !algData->chanDataDec.frame->Active()) {
        return util::ErrorEnum::FrameDataInvalid;
    }

    // Confidence thresholds
    std::vector<AiConfidence> confThres;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        confThres = params_.confidence;
    }

    std::vector<std::vector<AiDetectRstEl>> detRsts;
    std::vector<VideoFramePtr> images = {algData->chanDataDec.frame};
    duration_stat_.BeginSample();
    action_status_ = inst_->Detect(images, confThres, detRsts);
    duration_stat_.EndSample();

    if (!algData->chanDataDetect.detRet) {
        algData->chanDataDetect.detRet            = std::make_shared<DataDetTrackClassify>();
        algData->chanDataDetect.detRet->picWidth  = algData->chanDataDec.frame->GetWidth();
        algData->chanDataDetect.detRet->picHeight = algData->chanDataDec.frame->GetHeight();
    }
    algData->dataType = AlgDataType::ChannelDataDetect;
    if (!detRsts.empty())
        algData->chanDataDetect.detRet->targets.insert(algData->chanDataDetect.detRet->targets.end(),
                                                       detRsts[0].begin(), detRsts[0].end());

    std::vector<AiLabelParam> labelPos;
    TargetSignAreas(algData->chanDataDetect.detRet, task_area_.areas, task_area_.shieldedAreas, labelPos);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo