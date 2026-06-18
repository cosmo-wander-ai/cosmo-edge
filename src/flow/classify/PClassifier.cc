// Picture-mode classifier action implementation.
// Classifies detected targets in a single panoramic frame.

#include "flow/classify/PClassifier.h"

#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Keys.h"
#include "util/Log.h"

namespace cosmo {

PClassifier::~PClassifier() {
    LOG_INFO("[{}] Stop", GetTaskId(), GetFlowActionId());
    if (m_inst) {
        m_inst.reset();
        m_inst = nullptr;
    }
    LOG_INFO("[{} {}] Delete", GetTaskId(), GetFlowActionId());
}

PClassifier::PClassifier(const std::string& taskId, ActionNode& action)
    : PActionBase(action, taskId),
      m_durationStat(taskId + "-" + action.actionName + "-" + action.flowActionId) {
    m_actionStatus = util::ErrorEnum::ActionReady;
    LOG_INFO("[{} {}] Init ", GetTaskId(), GetFlowActionId());
}

bool PClassifier::ActionInit() {
    // Return early if SDK is already initialized.
    if (m_inst) {
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
    m_inst     = std::make_shared<AiClassifierUnify>(GetAtomicCode(), cfgPath, modelPath);
    auto aiRet = m_inst->Init();
    if (util::ErrorEnum::Success != aiRet) {
        m_inst.reset();
        m_inst         = nullptr;
        m_actionStatus = aiRet;
        LOG_WARN("[{} {}] Sdk Init Failed code:{}", GetTaskId(), GetFlowActionId(), aiRet);
        return false;
    }

    m_actionStatus = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("[{} {}] Init Sdk", GetTaskId(), GetFlowActionId());
    return true;
}

// Key format reference: aiParam.#{labelCode}.confidence
bool PClassifier::ValidKey(MsgDynamicKeyValue& param) {
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

bool PClassifier::AnalysisKey(MsgDynamicKeyValue& param) {
    if (!ValidKey(param)) {
        return false;
    }

    return false;
}

// Modify parameters - update existing parameters incrementally.
bool PClassifier::ModifyParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

// Set parameters - clear all existing parameters and apply new ones.
bool PClassifier::SetParam(const std::string& /*taskId*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(m_mtx);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

util::ErrorEnum PClassifier::HandPic(AlgDataPtr algData) {
    if (!algData || !algData->chanDataDec.frame || !algData->chanDataDec.frame->Active()) {
        return util::ErrorEnum::FrameDataInvalid;
    }

    if (!algData->chanDataDetect.detRet) {
        return util::ErrorEnum::FlowDataNull;
    }

    if (algData->chanDataDetect.detRet->targets.empty()) {
        return util::ErrorEnum::Success;
    }

    m_durationStat.BeginSample();
    m_actionStatus = m_inst->Classify(algData->chanDataDec.frame, algData->chanDataDetect.detRet->targets);
    m_durationStat.EndSample();

    algData->dataType = AlgDataType::TaskDataClassify;
    return util::ErrorEnum::Success;
}

}  // namespace cosmo