#include "flow/detect/AiPersonFace.h"

#include <cmath>

#include "flow/target/PersonFaceAssociation.h"
#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "util/SafeParse.h"

namespace cosmo {

AiPersonFace::AiPersonFace(const std::string& task_id, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiPersonFace, action, "", task_id),
      model_code_(action.atomicCode.empty() ? action.atomAlgName : action.atomicCode) {
    ModifyParam("", task_id, action.configObject.params);
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
    for (const auto& param : params) {
        if (param.key.ToString() == "param.minFaceSize") {
            const int value = util::ParseInt(param.value, -1);
            if (value >= 10 && value <= 1000) {
                min_face_size_ = value;
            }
        } else if (param.key.ToString() == "param.faceDetectionConfidence") {
            const float value = util::ParseFloat(param.value, -1.0f);
            if (std::isfinite(value) && value >= 0.0f && value <= 1.0f) {
                face_confidence_ = value;
            }
        }
    }
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
    int min_size;
    float confidence;
    {
        std::shared_lock<std::shared_mutex> lock(mtx);
        min_size   = min_face_size_;
        confidence = face_confidence_;
    }
    std::vector<AiDetectRstEl> faces;
    action_status = util::ErrorEnum::Success;
    if (!tracks->targets.empty()) {
        AiConfidence threshold;
        threshold.label       = "face";
        threshold.atomic_code = model_code_;
        threshold.confidence  = confidence;
        action_status = InitDetector() ? detector_->Detect(data->chanDataDec.frame, {threshold}, faces)
                                       : util::ErrorEnum::AI_INST_NOTCREATED;
    }
    result->observation_complete = tracks->observation_complete && action_status == util::ErrorEnum::Success;
    if (!result->observation_complete) {
        faces.clear();
    }
    AssociatePersonFaces(result->targets, faces, min_size);
    result->dataType = AlgDataType::TaskDataPersonFace;
    output->SetTaskResult(AlgDataType::TaskDataPersonFace, result);
    output->dataType     = AlgDataType::TaskDataPersonFace;
    output->bHaveRelated = true;
    distributor->DistributorData(output);
}

}  // namespace cosmo
