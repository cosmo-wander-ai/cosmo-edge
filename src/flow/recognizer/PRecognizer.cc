// PRecognizer — P Recognizer implementation.

#include "flow/recognizer/PRecognizer.h"

#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/StringUtil.h"

namespace cosmo {

PRecognizer::~PRecognizer() {
    LOG_INFO("[{} {}] Stop", GetTaskId(), GetFlowActionId());
    if (inst_) {
        inst_.reset();
        inst_ = nullptr;
    }
    LOG_INFO("[{} {}] Delete", GetTaskId(), GetFlowActionId());
}

PRecognizer::PRecognizer(const std::string& task_id, ActionNode& action)
    : PActionBase(action, task_id),
      duration_stat_(task_id + "-" + action.actionName + "-" + action.flowActionId) {
    action_status_pr_ = util::ErrorEnum::ActionReady;
    LOG_INFO("[{} {}] Init ", GetTaskId(), GetFlowActionId());
}

bool PRecognizer::ActionInit() {
    if (inst_) {
        LOG_INFO("[{} {}] Sdk Have Init", GetTaskId(), GetFlowActionId());
        return true;
    }

    std::string cfg_path   = "";
    std::string model_path = "";
    auto cfg_ret           = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
                  GetAtomicCode(), cfg_path, model_path);
    if (!cfg_ret) {
        LOG_WARN("Get Model Configure Failed. AlgCode:{} code:{}", GetAtomicCode(), cfg_ret);
        return false;
    }
    auto pool = service::ServiceRegistry::Instance().Get<service::IInferPoolService>().GetRecognizerPool(
        GetAtomicCode());
    inst_ = std::make_shared<AiRecognizerInterface>(pool, GetAtomicCode(), cfg_path, model_path);

    action_status_pr_ = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("[{} {}] Init Sdk", GetTaskId(), GetFlowActionId());
    return true;
}

bool PRecognizer::AnalysisKey(MsgDynamicKeyValue& param) {
    const std::string key_str(util::Trim(param.key.ToRefString()));

    // param.faceSet
    const bool is_face_set_key =
        (key_str == "param.faceSet") ||
        (param.keys.size() == 2 && param.keys[0] == key::PARAM && param.keys[1] == key::target::FACE_SET);
    if (is_face_set_key) {
        const auto v = util::Trim(param.value.ToRefString());
        if (v.empty()) {
            return false;
        }
        face_set_.clear();
        for (auto tok : util::Split(v, ",")) {
            const auto t = util::Trim(tok);
            if (!t.empty()) {
                face_set_.emplace_back(std::string(t));
            }
        }
        LOG_INFO("ModifyParam [{} {}] Set {} To {}", GetTaskId(), GetFlowActionId(), param.key, param.value);
        return true;
    }

    return false;
}

bool PRecognizer::ModifyParam(const std::string& /*task_id*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

bool PRecognizer::SetParam(const std::string& /*task_id*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    face_set_.clear();
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

util::ErrorEnum PRecognizer::HandPic(AlgDataPtr alg_data) {
    if (!alg_data || !alg_data->chanDataDec.frame || !alg_data->chanDataDec.frame->Active()) {
        return util::ErrorEnum::FrameDataInvalid;
    }

    if (!alg_data->chanDataDetect.detRet) {
        return util::ErrorEnum::FlowDataNull;
    }

    if (alg_data->chanDataDetect.detRet->targets.empty()) {
        return util::ErrorEnum::Success;
    }

    // If previous step provides landmark, use landmark for feature extraction; otherwise use box
    bool use_box = !alg_data->bHaveLandmark;

    duration_stat_.BeginSample();
    action_status_pr_ =
        inst_->Recognize(alg_data->chanDataDec.frame, alg_data->chanDataDetect.detRet->targets, use_box);
    duration_stat_.EndSample();

    if (action_status_pr_ != util::ErrorEnum::Success) {
        LOG_WARN("[{} {}] Recognize Failed", GetTaskId(), GetFlowActionId());
        return action_status_pr_;
    }

    // In pure feature extraction mode for image algorithms, TaskDataRecognizer is not set (to avoid
    // triggering comparison logic). dataType remains unchanged, and feature values are written to
    // target.feature
    LOG_INFO("[{} {}] Recognize targets:{} useBox:{}", GetTaskId(), GetFlowActionId(),
             alg_data->chanDataDetect.detRet->targets.size(), use_box);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
