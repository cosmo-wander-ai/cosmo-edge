// PRecognizer — P Recognizer implementation.

#include "flow/recognizer/PRecognizer.h"

#include <cmath>

#include "flow/recognizer/PFaceCompare.h"
#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
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
    for (const auto& param : action.configObject.params) {
        if (param.key.ToString() == "featureInput") {
            face_input_ = param.value.ToString() == "0";
        }
    }
    LOG_INFO("[{} {}] Init ", GetTaskId(), GetFlowActionId());
}

bool PRecognizer::ActionInit() {
    if (inst_) {
        LOG_INFO("[{} {}] Sdk Have Init", GetTaskId(), GetFlowActionId());
        return true;
    }

    std::string cfg_path   = "";
    std::string model_path = "";
    auto cfg_ret = service::ServiceRegistry::Instance().Get<service::IModelPathMapping>().GetModelCfg(
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

    if (key_str == "param.faceCompare") {
        const auto value = param.value.ToString();
        if (value != "0" && value != "1") {
            params_valid_ = false;
            return false;
        }
        compare_enabled_ = value == "1";
        return true;
    }
    if (key_str == "param.limitScore") {
        try {
            const auto value  = param.value.ToString();
            size_t consumed   = 0;
            const float score = std::stof(value, &consumed);
            if (consumed != value.size() || !std::isfinite(score) || score < 0 || score > 100) {
                params_valid_ = false;
                return false;
            }
            limit_score_ = score;
            return true;
        } catch (const std::exception&) {
            params_valid_ = false;
            return false;
        }
    }

    return false;
}

bool PRecognizer::ModifyParam(const std::string& /*task_id*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    params_valid_ = true;
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

bool PRecognizer::SetParam(const std::string& /*task_id*/, std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    face_set_.clear();
    compare_enabled_ = false;
    params_valid_    = true;
    limit_score_     = 0.0F;
    for (auto& param : params) {
        AnalysisKey(param);
    }
    return true;
}

util::ErrorEnum PRecognizer::EnsureFaceLandmarks(VideoFramePtr frame, std::vector<AiDetectRstEl>& targets) {
    // The enrollment embedding model requires five face landmarks, including when a
    // custom picture workflow connects detection directly to feature extraction.
    std::vector<AiDetectRstEl> pending;
    std::vector<size_t> indices;
    for (size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        const auto& points =
            target.relatedEl.bActive ? target.relatedEl.landmark.landmark : target.landmark.landmark;
        if (points.size() != 5) {
            indices.push_back(i);
            pending.push_back(target);
            auto& copy = pending.back();
            (copy.relatedEl.bActive ? copy.relatedEl.landmark.landmark : copy.landmark.landmark).clear();
        }
    }
    if (pending.empty()) {
        return util::ErrorEnum::Success;
    }
    if (!face_landmark_inst_) {
        constexpr const char* kFaceLandmarkCode = "1000016";
        auto& registry                          = service::ServiceRegistry::Instance();
        std::string cfg_path;
        std::string model_path;
        if (!registry.Get<service::IModelPathMapping>().GetModelCfg(kFaceLandmarkCode, cfg_path,
                                                                    model_path)) {
            LOG_WARN("[{} {}] Face landmark model is unavailable", GetTaskId(), GetFlowActionId());
            return util::ErrorEnum::ModelFileLack;
        }
        auto pool = registry.Get<service::IInferPoolService>().GetLandmarkPool(kFaceLandmarkCode);
        if (!pool) {
            return util::ErrorEnum::NotInit;
        }
        face_landmark_inst_ =
            std::make_shared<AiLandmarkInterface>(pool, kFaceLandmarkCode, cfg_path, model_path);
    }
    auto result = face_landmark_inst_->Marker(frame, pending);
    if (result != util::ErrorEnum::Success) {
        return result;
    }
    for (size_t i = 0; i < pending.size(); ++i) {
        const auto& target = pending[i];
        const auto& points =
            target.relatedEl.bActive ? target.relatedEl.landmark.landmark : target.landmark.landmark;
        if (points.size() != 5) {
            return util::ErrorEnum::GetFeatureFailed;
        }
        targets[indices[i]] = std::move(pending[i]);
    }
    LOG_INFO("[{} {}] Prepared face landmarks for {} targets", GetTaskId(), GetFlowActionId(),
             indices.size());
    return util::ErrorEnum::Success;
}

util::ErrorEnum PRecognizer::HandPic(AlgDataPtr alg_data) {
    std::unique_lock<std::shared_mutex> lock(mtx_);
    if (!params_valid_)
        return util::ErrorEnum::InvalidParam;
    if (compare_enabled_ && (!face_input_ || face_set_.empty())) {
        return util::ErrorEnum::InvalidParam;
    }
    // Enrollment currently uses this fixed embedding model. Reject incompatible models
    // instead of comparing vectors that may have equal dimensions but different semantics.
    if (compare_enabled_ && GetAtomicCode() != "1000005") {
        return util::ErrorEnum::InvalidParam;
    }
    if (!alg_data || !alg_data->chanDataDec.frame || !alg_data->chanDataDec.frame->Active()) {
        return util::ErrorEnum::FrameDataInvalid;
    }

    if (!alg_data->chanDataDetect.detRet) {
        return util::ErrorEnum::FlowDataNull;
    }

    if (alg_data->chanDataDetect.detRet->targets.empty()) {
        return util::ErrorEnum::Success;
    }

    // Other embedding models retain their existing box/landmark input contract.
    bool use_box = !alg_data->bHaveLandmark;
    if (face_input_ && GetAtomicCode() == "1000005") {
        auto result =
            EnsureFaceLandmarks(alg_data->chanDataDec.frame, alg_data->chanDataDetect.detRet->targets);
        if (result != util::ErrorEnum::Success) {
            return result;
        }
        alg_data->bHaveLandmark = true;
        use_box                 = false;
    }
    if (!inst_) {
        return util::ErrorEnum::NotInit;
    }

    duration_stat_.BeginSample();
    action_status_pr_ =
        inst_->Recognize(alg_data->chanDataDec.frame, alg_data->chanDataDetect.detRet->targets, use_box);
    duration_stat_.EndSample();

    if (action_status_pr_ != util::ErrorEnum::Success) {
        LOG_WARN("[{} {}] Recognize Failed", GetTaskId(), GetFlowActionId());
        return action_status_pr_;
    }

    if (compare_enabled_) {
        auto& registry = service::ServiceRegistry::Instance();
        return ComparePictureFaces(alg_data->chanDataDetect.detRet->targets, face_set_, limit_score_,
                                   registry.Get<service::IFaceLibRepo>(),
                                   registry.Get<service::IFaceFeature>());
    }

    // In pure feature extraction mode for image algorithms, TaskDataRecognizer is not set (to avoid
    // triggering comparison logic). dataType remains unchanged, and feature values are written to
    // target.feature
    LOG_INFO("[{} {}] Recognize targets:{} useBox:{}", GetTaskId(), GetFlowActionId(),
             alg_data->chanDataDetect.detRet->targets.size(), use_box);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
