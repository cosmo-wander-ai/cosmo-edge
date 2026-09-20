#include "flow/detect/AiPersonFace.h"

#include "flow/target/PersonFaceAssociation.h"
#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"

namespace cosmo {

AiPersonFace::AiPersonFace(const std::string& init_task_id, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiPersonFace, action, "", init_task_id),
      model_code_(action.atomicCode.empty() ? action.atomAlgName : action.atomicCode) {
    config_valid_ = ModifyParam("", init_task_id, action.configObject.params);
}

AiPersonFace::~AiPersonFace() {
    Stop();
}

bool AiPersonFace::InitDetector() {
    if (detector_) {
        return true;
    }
    std::string cfg_path;
    std::string model_path;
    auto& registry = service::ServiceRegistry::Instance();
    if (!registry.Get<service::IModelPathMapping>().GetModelCfg(model_code_, cfg_path, model_path)) {
        return false;
    }
    auto pool = registry.Get<service::IInferPoolService>().GetDetectPool(model_code_);
    detector_ = std::make_shared<infer::AiDetectInterface>(pool, model_code_, cfg_path, model_path);
    return true;
}

bool AiPersonFace::ModifyParam(const std::string&, const std::string&,
                               std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    if (!UpdateTargetAssociationConfig(config_, params))
        return false;
    config_valid_ = true;
    return true;
}

void AiPersonFace::HandFrame(AlgDataPtr data) {
    if (!data || data->dataType != AlgDataType::TaskDataTrack || !data->chanDataDec.frame) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    auto output = AlgDataCopy(data);
    auto tracks = output->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!tracks) {
        return;
    }
    auto result = std::make_shared<DataDetTrackClassify>(*tracks);
    TargetAssociationConfig config;
    bool config_valid;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        config       = config_;
        config_valid = config_valid_;
    }
    std::vector<AiDetectRstEl> children;
    action_status = util::ErrorEnum::Success;
    if (!config_valid || config.labels.empty()) {
        action_status = util::ErrorEnum::FlowDataInvalid;
    } else if (!tracks->targets.empty() && tracks->observation_complete) {
        std::vector<AiConfidence> thresholds;
        for (const auto& label : config.labels) {
            AiConfidence threshold;
            threshold.label       = label;
            threshold.atomic_code = model_code_;
            threshold.confidence  = config.confidence;
            thresholds.push_back(std::move(threshold));
        }
        action_status = InitDetector() ? detector_->Detect(data->chanDataDec.frame, thresholds, children)
                                       : util::ErrorEnum::AI_INST_NOTCREATED;
    }
    result->observation_complete = tracks->observation_complete && action_status == util::ErrorEnum::Success;
    AssociateTargets(result->targets, children, config, result->observation_complete);
    if (config.IsFaceAssociation())
        UpdateAssociatedFaceObservations(result->targets);
    result->dataType =
        config.IsFaceAssociation() ? AlgDataType::TaskDataPersonFace : AlgDataType::TaskDataAssoTarget;
    output->SetTaskResult(result->dataType, result);
    output->dataType     = result->dataType;
    output->bHaveRelated = true;
    distributor->DistributorData(output);
}

}  // namespace cosmo
