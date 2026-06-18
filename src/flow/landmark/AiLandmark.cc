// AI landmark keypoint detection action for video stream pipeline.

#include "flow/landmark/AiLandmark.h"

#include "flow/common/FlowTaskUtil.h"
#include "service/ai/IInferPoolService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag       = "AI-LANDMARK ";
static constexpr int kFilterLogInterval = 100;

namespace cosmo {

AiLandmark::~AiLandmark() {
    LOG_INFO("{}[{} {}] Stop", kTag, task_id, uuid);
    Stop();
    if (inst_) {
        inst_.reset();
    }
    LOG_INFO("{}[{} {}] Delete", kTag, task_id, uuid);
}

AiLandmark::AiLandmark(const std::string& a_task_id, const std::string& alg_code, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionAiLandmark, action, "", a_task_id), alg_code_(alg_code) {
    action_status = util::ErrorEnum::ActionReady;
    uuid          = util::GenerateUUID();
    LOG_INFO("{}[{} {}] Init ", kTag, task_id, uuid);
}

bool AiLandmark::AiSdkInit() {
    // Return early if SDK is already initialized.
    if (inst_) {
        LOG_INFO("{}[{} {}] Sdk Have Init", kTag, task_id, uuid);
        return true;
    }

    std::string cfg_path;
    std::string model_path;
    auto cfg_ret = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelCfg(
        alg_code_, cfg_path, model_path);
    if (!cfg_ret) {
        LOG_WARN("{}Get Model Configure Failed. AlgCode:{} code:{}", kTag, alg_code_, cfg_ret);
        return false;
    }
    auto pool =
        service::ServiceRegistry::Instance().Get<service::IInferPoolService>().GetLandmarkPool(alg_code_);
    inst_         = std::make_shared<AiLandmarkInterface>(pool, alg_code_, cfg_path, model_path);
    action_status = util::ErrorEnum::AI_INST_CREATED;
    LOG_INFO("{}[{} {}] Init Sdk", kTag, task_id, uuid);
    return true;
}

bool AiLandmark::SetArea(const std::string& /*channel_id*/, const std::string& task_id_param,
                         std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shielded_areas) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    for (auto& area : areas) {
        AreaToLocalBox(area);
    }
    task_area_.taskId        = task_id_param;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shielded_areas;
    return true;
}

void AiLandmark::HandFrame(AlgDataPtr alg_data) {
    if (!alg_data) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{} {}] Filter {} Frames", kTag, task_id, uuid, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    if (!inst_) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{} {}] Filter {} Frames", kTag, task_id, uuid, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit()) {
            return;
        }
    }

    if (!alg_data->chanDataDec.frame || !alg_data->chanDataDec.frame->Active()) {
        action_status = util::ErrorEnum::FrameDataInvalid;
        return;
    }

    // Validate input data type.
    if (!((AlgDataType::ChannelDataDec == alg_data->dataType) ||
          (AlgDataType::ChannelDataDetect == alg_data->dataType) ||
          (AlgDataType::TaskDataTrack == alg_data->dataType) ||
          (AlgDataType::TaskDataPersonFace == alg_data->dataType) ||
          (AlgDataType::TaskDataFaceLogic == alg_data->dataType) ||
          (AlgDataType::TaskDataClassify == alg_data->dataType))) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % kFilterLogInterval) {
            LOG_WARN("{}[{} {}] Filter {} Frames dataType:{}", kTag, task_id, uuid, invalid_frame_cnt,
                     alg_data->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    // Copy input and prepare landmark output data.
    AlgDataPtr alg_out_data = AlgDataCopy(alg_data);
    alg_out_data->dataType  = AlgDataType::TaskDataLandmark;

    // Initialize landmark task result container.
    auto landmark = std::make_shared<DataDetTrackClassify>();
    alg_out_data->SetTaskResult(AlgDataType::TaskDataLandmark, landmark);

    auto input = alg_out_data->GetTaskResult(AlgDataType::TaskDataTrack);
    if (AlgDataType::ChannelDataDetect == alg_data->dataType) {
        input = alg_out_data->chanDataDetect.detRet;
    } else if (AlgDataType::TaskDataTrack == alg_data->dataType) {
        input = alg_out_data->GetTaskResult(AlgDataType::TaskDataTrack);
    } else if (AlgDataType::TaskDataClassify == alg_data->dataType) {
        input = alg_out_data->GetTaskResult(AlgDataType::TaskDataClassify);
    } else if (AlgDataType::TaskDataFaceLogic == alg_data->dataType) {
        input = alg_out_data->GetTaskResult(AlgDataType::TaskDataFaceLogic);
    }

    bool has_upstream_target_source = FlowHasUpstreamTargetSource(action_alg, action_node.flowActionId);
    if (!input && has_upstream_target_source) {
        return;
    }

    std::vector<AiDetectRstEl> targets = input ? input->targets : std::vector<AiDetectRstEl>{};
    if (targets.empty() && !has_upstream_target_source) {
        std::vector<MsgTaskArea> areas;
        {
            std::shared_lock<std::shared_mutex> lock(mtx_);
            areas = task_area_.areas;
        }
        auto frame = alg_out_data->chanDataDec.frame;
        targets =
            BuildAreaFallbackTargets(areas, frame ? frame->GetWidth() : 0, frame ? frame->GetHeight() : 0);
    }

    if (targets.empty()) {
        return;
    }

    landmark->targets           = targets;
    landmark->bHaveArea         = input ? input->bHaveArea : true;
    landmark->bHaveShieldedArea = input ? input->bHaveShieldedArea : false;
    auto frame                  = alg_out_data->chanDataDec.frame;
    landmark->frameIndex        = input ? input->frameIndex : frame->GetFrameIndex();
    landmark->streamIndex       = input ? input->streamIndex : frame->GetStreamIndex();
    landmark->timestamp         = input ? input->timestamp : frame->GetTimestamp();
    landmark->picWidth          = input ? input->picWidth : frame->GetWidth();
    landmark->picHeight         = input ? input->picHeight : frame->GetHeight();
    landmark->dataType          = input ? input->dataType : AlgDataType::TaskDataLandmark;
    if (input) {
        landmark->reportType = input->reportType;
        landmark->areaInfo   = input->areaInfo;
    }

    action_status = inst_->Marker(alg_out_data->chanDataDec.frame, landmark->targets);

    if (action_status != util::ErrorEnum::Success) {
        LOG_WARN("{}", "landmark failed");
        return;
    }

    alg_out_data->bHaveLandmark = true;
    // Distribute result to downstream queues.
    distributor->DistributorData(alg_out_data);
}
}  // namespace cosmo
