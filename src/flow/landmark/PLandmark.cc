// Picture-mode landmark keypoint detection action.
// Input: algData from detection/classification stage (with detRet->targets).
// Processing: runs landmark marker on each target, writes to target.landmark.

#include "flow/landmark/PLandmark.h"

#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Log.h"

namespace cosmo {

PLandmark::~PLandmark() {
    LOG_INFO("[{} {}] Stop", GetTaskId(), GetFlowActionId());
    if (inst_) {
        inst_.reset();
    }
    LOG_INFO("[{} {}] Delete", GetTaskId(), GetFlowActionId());
}

PLandmark::PLandmark(const std::string& task_id, ActionNode& action)
    : PActionBase(action, task_id),
      duration_stat_(task_id + "-" + action.actionName + "-" + action.flowActionId) {
    action_status_ = util::ErrorEnum::ActionReady;
    LOG_INFO("[{} {}] Init ", GetTaskId(), GetFlowActionId());
}

bool PLandmark::ActionInit() {
    if (inst_) {
        LOG_INFO("[{} {}] Sdk Have Init", GetTaskId(), GetFlowActionId());
        return true;
    }

    std::string cfg_path;
    std::string model_path;
    auto cfg_ret = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
        GetAtomicCode(), cfg_path, model_path);
    if (!cfg_ret) {
        LOG_WARN("Get Model Configure Failed. AlgCode:{} code:{}", GetAtomicCode(), cfg_ret);
        return false;
    }
    auto pool = service::ServiceRegistry::Instance().Get<service::IInferPoolService>().GetLandmarkPool(
        GetAtomicCode());
    inst_          = std::make_shared<AiLandmarkInterface>(pool, GetAtomicCode(), cfg_path, model_path);
    action_status_ = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("[{} {}] Init Sdk", GetTaskId(), GetFlowActionId());
    return true;
}

util::ErrorEnum PLandmark::HandPic(AlgDataPtr alg_data) {
    if (!alg_data || !alg_data->chanDataDec.frame || !alg_data->chanDataDec.frame->Active()) {
        return util::ErrorEnum::FrameDataInvalid;
    }

    if (!alg_data->chanDataDetect.detRet) {
        return util::ErrorEnum::FlowDataNull;
    }

    if (alg_data->chanDataDetect.detRet->targets.empty()) {
        return util::ErrorEnum::Success;
    }

    duration_stat_.BeginSample();
    action_status_ = inst_->Marker(alg_data->chanDataDec.frame, alg_data->chanDataDetect.detRet->targets);
    duration_stat_.EndSample();

    if (action_status_ != util::ErrorEnum::Success) {
        LOG_WARN("[{} {}] Landmark Failed", GetTaskId(), GetFlowActionId());
        return action_status_;
    }

    alg_data->bHaveLandmark = true;
    alg_data->dataType      = AlgDataType::TaskDataLandmark;

    LOG_INFO("[{} {}] Landmark targets:{}", GetTaskId(), GetFlowActionId(),
             alg_data->chanDataDetect.detRet->targets.size());
    return util::ErrorEnum::Success;
}

}  // namespace cosmo
