#include "flow/landmark/AiLandmark.h"

namespace cosmo {

void AiLandmark::HandAssociatedFaces(AlgDataPtr data) {
    if (!data->chanDataDec.frame) {
        return;
    }
    auto input = data->GetTaskResult(data->dataType);
    if (!input) {
        return;
    }
    auto output      = AlgDataCopy(data);
    auto result      = std::make_shared<DataDetTrackClassify>(*input);
    result->dataType = AlgDataType::TaskDataLandmark;
    std::vector<AiDetectRstEl> candidates;
    std::vector<size_t> indexes;
    for (size_t i = 0; i < result->targets.size(); ++i) {
        auto& target              = result->targets[i];
        target.relatedEl.landmark = {};
        if (!result->observation_complete || !target.relatedEl.bActive || target.bFilter ||
            !target.areaSign.shielded_areas.empty() || (result->bHaveArea && target.areaSign.areas.empty())) {
            continue;
        }
        indexes.push_back(i);
        candidates.push_back(target);
    }
    action_status = util::ErrorEnum::Success;
    if (!candidates.empty()) {
        action_status = AiSdkInit() ? inst_->Marker(data->chanDataDec.frame, candidates)
                                    : util::ErrorEnum::AI_INST_NOTCREATED;
        if (action_status == util::ErrorEnum::Success) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                result->targets[indexes[i]] = std::move(candidates[i]);
            }
        } else {
            result->observation_complete = false;
        }
    }
    output->SetTaskResult(AlgDataType::TaskDataLandmark, result);
    output->dataType      = AlgDataType::TaskDataLandmark;
    output->bHaveLandmark = true;
    distributor->DistributorData(output);
}

}  // namespace cosmo
