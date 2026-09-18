#include <algorithm>
#include <cmath>

#include "flow/recognizer/AiRecognizer.h"
#include "flow/recognizer/FaceObservationPolicy.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IFaceFeature.h"

namespace cosmo {

void AiRecognizer::HandObservations(AlgDataPtr data) {
    if (data->dataType != AlgDataType::TaskDataLandmark || !data->bHaveRelated || !data->chanDataDec.frame) {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    auto input = data->GetTaskResult(AlgDataType::TaskDataLandmark);
    if (!input) {
        return;
    }
    AiRecognizerParam params;
    float limit_score;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        params      = params_;
        limit_score = alg_params_.limit_score;
    }
    std::sort(params.face_set.begin(), params.face_set.end());
    params.face_set.erase(std::unique(params.face_set.begin(), params.face_set.end()), params.face_set.end());
    auto output                 = AlgDataCopy(data);
    auto result                 = std::make_shared<DataDetTrackClassify>(*input);
    result->dataType            = AlgDataType::TaskDataRecognizer;
    result->recognition_context = std::to_string(limit_score) + ":" +
                                  std::to_string(params.min_face_quality) + ":" +
                                  std::to_string(params.front_face_only);
    for (const auto& set : params.face_set) {
        result->recognition_context += ":" + std::to_string(set.size()) + ":" + set;
    }
    std::vector<AiDetectRstEl> candidates;
    std::vector<size_t> indexes;
    for (size_t i = 0; i < result->targets.size(); ++i) {
        auto& target             = result->targets[i];
        target.feature           = {};
        target.relatedEl.feature = {};
        target.matchInfo         = {};
        target.bLogicResult      = false;
        target.face_observation =
            AssessFaceObservation(target, input->bHaveArea, params.min_face_quality, params.front_face_only);
        if (!input->observation_complete || params.face_set.empty()) {
            target.face_observation.status = FaceObservationStatus::kUnavailable;
        }
        if (target.face_observation.status == FaceObservationStatus::kNotObserved) {
            indexes.push_back(i);
            candidates.push_back(target);
        }
    }
    action_status = util::ErrorEnum::Success;
    if (!candidates.empty()) {
        action_status = AiSdkInit() ? inst_->Recognize(data->chanDataDec.frame, candidates, false)
                                    : util::ErrorEnum::AI_INST_NOTCREATED;
        for (size_t i = 0; i < candidates.size(); ++i) {
            auto& target  = result->targets[indexes[i]];
            auto& feature = candidates[i].feature;
            if (action_status != util::ErrorEnum::Success || feature.feature.empty() ||
                !std::all_of(feature.feature.begin(), feature.feature.end(),
                             [](float value) { return std::isfinite(value); })) {
                target.face_observation.status = FaceObservationStatus::kUnavailable;
                continue;
            }
            target.feature = std::move(feature);
            service::ServiceRegistry::Instance().Get<service::IFaceFeature>().FaceCompare(
                params.face_set, target.feature, target.matchInfo, limit_score);
            target.face_observation.status = FaceComparisonStatus(target.matchInfo);
            target.bLogicResult = target.face_observation.status == FaceObservationStatus::kMatched;
        }
    }
    result->observation_complete = result->observation_complete && action_status == util::ErrorEnum::Success;
    output->SetTaskResult(AlgDataType::TaskDataRecognizer, result);
    output->dataType = AlgDataType::TaskDataRecognizer;
    output->taskDataAlarm.alarmData.reset();
    DistributorData(output);
}

}  // namespace cosmo
