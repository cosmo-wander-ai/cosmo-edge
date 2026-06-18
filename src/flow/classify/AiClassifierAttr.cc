// AI attribute classifier action implementation.
// Performs attribute classification on detected targets via registered queues.

#include "flow/classify/AiClassifierAttr.h"

#include "flow/common/AlgDataRecord.h"
#include "flow/common/FlowTaskUtil.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "util/Log.h"

namespace cosmo {

AiClassifierAttr::AiClassifierAttr(const std::string& taskId, ActionNode& action)
    : AiClassifierBase(AlgActionType::AlgActionAiClassifyAttr, taskId, action, "AI-CLASSIFIER-ATTR ",
                       "attribute_" + action.atomicCode) {
    batch_count_ = 2;
    LOG_INFO("{}[{} {} {}] Init ", log_tag_, GetTaskId(), GetName(), uuid);
}

bool AiClassifierAttr::CheckDataAvailable(AlgDataPtr algData) {
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{} {} {}] Filter {} Frames", log_tag_, GetTaskId(), GetName(), uuid,
                     invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return false;
    }
    if (!classifier_) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{} {} {}] Filter {} Frames", log_tag_, GetTaskId(), GetName(), uuid,
                     invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::AI_INST_NOTCREATED;
        if (!AiSdkInit()) {
            return false;
        }
    }

    if (!algData->chanDataDec.frame || !algData->chanDataDec.frame->Active()) {
        action_status = util::ErrorEnum::FrameDataInvalid;
        return false;
    }

    // Validate the incoming data type is supported for classification.
    if (!((AlgDataType::ChannelDataDec == algData->dataType) ||
          (AlgDataType::ChannelDataDetect == algData->dataType) ||
          (AlgDataType::TaskDataTrack == algData->dataType) ||
          (AlgDataType::TaskDataPersonFace == algData->dataType) ||
          (AlgDataType::TaskDataClassify == algData->dataType) ||
          (AlgDataType::TaskDataLandmark == algData->dataType))) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{} {} {}] Filter {} Frames dataType:{}", log_tag_, GetTaskId(), GetName(), uuid,
                     invalid_frame_cnt, algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return false;
    }

    return true;
}

void AiClassifierAttr::HandFramesEx(std::vector<AlgDataPtr> alg_datas) {
    std::vector<AlgDataPtr> alg_out_datas;
    std::vector<VideoFramePtr> in_images;
    std::vector<std::vector<AiDetectRstEl>> io_puts;
    std::vector<int> img_record;
    size_t image_num = alg_datas.size();
    for (size_t i = 0; i < image_num; i++) {
        auto alg_data           = alg_datas[i];
        AlgDataPtr alg_out_data = AlgDataCopy(alg_data);
        // Attribute classification does not modify the data type.
        auto classify_result = std::make_shared<DataDetTrackClassify>();
        alg_out_data->SetTaskResult(AlgDataType::TaskDataClassify, classify_result);
        if (alg_data->chanDataDetect.detRet) {
            classify_result->frameIndex        = alg_data->chanDataDetect.detRet->frameIndex;
            classify_result->streamIndex       = alg_data->chanDataDetect.detRet->streamIndex;
            classify_result->timestamp         = alg_data->chanDataDetect.detRet->timestamp;
            classify_result->picWidth          = alg_data->chanDataDetect.detRet->picWidth;
            classify_result->picHeight         = alg_data->chanDataDetect.detRet->picHeight;
            classify_result->bHaveArea         = alg_data->chanDataDetect.detRet->bHaveArea;
            classify_result->bHaveShieldedArea = alg_data->chanDataDetect.detRet->bHaveShieldedArea;
        } else if (alg_data->chanDataDec.frame) {
            classify_result->frameIndex  = alg_data->chanDataDec.frame->GetFrameIndex();
            classify_result->streamIndex = alg_data->chanDataDec.frame->GetStreamIndex();
            classify_result->timestamp   = alg_data->chanDataDec.frame->GetTimestamp();
            classify_result->picWidth    = alg_data->chanDataDec.frame->GetWidth();
            classify_result->picHeight   = alg_data->chanDataDec.frame->GetHeight();
        }

        auto input = alg_data->GetTaskResult(AlgDataType::TaskDataTrack);
        if (AlgDataType::ChannelDataDetect == alg_data->dataType) {
            input = alg_data->chanDataDetect.detRet;
        } else if (AlgDataType::TaskDataClassify == alg_data->dataType) {
            input = alg_data->GetTaskResult(AlgDataType::TaskDataClassify);
        } else if (AlgDataType::TaskDataLandmark == alg_data->dataType) {
            input = alg_data->GetTaskResult(AlgDataType::TaskDataLandmark);
        }

        if (input) {
            classify_result->bHaveArea         = input->bHaveArea;
            classify_result->bHaveShieldedArea = input->bHaveShieldedArea;
        }
        alg_out_datas.push_back(alg_out_data);

        bool has_upstream_target_source = FlowHasUpstreamTargetSource(action_alg, action_node.flowActionId);
        if (!input && has_upstream_target_source) {
            continue;
        }

        std::vector<AiDetectRstEl> io_puts_el;
        std::vector<AiDetectRstEl> targets = input ? input->targets : std::vector<AiDetectRstEl>{};
        if (targets.empty() && !has_upstream_target_source) {
            std::vector<MsgTaskArea> areas;
            {
                std::shared_lock<std::shared_mutex> lock(mtx);
                areas = task_area_.areas;
            }
            targets = BuildAreaFallbackTargets(areas, classify_result->picWidth, classify_result->picHeight);
            classify_result->bHaveArea = !targets.empty();
        }
        size_t box_nums = targets.size();
        for (size_t j = 0; j < box_nums; j++) {
            if ((!targets[j].bForceClassify) &&
                ((targets[j].bFilter)                                   // Skip filtered targets
                 || (input && input->bHaveArea                          // When areas are defined
                     && ((!targets[j].areaSign.shielded_areas.empty())  // Skip targets in shielded areas
                         || (targets[j].areaSign.areas.empty())))))     // Skip targets outside all areas
            {
                classify_result->targets.push_back(targets[j]);
                continue;
            }
            io_puts_el.push_back(targets[j]);
        }

        if (io_puts_el.size() < 1) {
            continue;
        }

        io_puts.push_back(io_puts_el);
        in_images.push_back(alg_data->chanDataDec.frame);
        img_record.push_back(i);
    }

    action_status = classifier_->Classify(in_images, io_puts);
    for (size_t out = 0; out < io_puts.size(); out++) {
        auto classify_result = alg_out_datas[img_record[out]]->GetTaskResult(AlgDataType::TaskDataClassify);
        if (classify_result) {
            for (const auto& el : io_puts[out]) {
                classify_result->targets.push_back(el);
            }

            auto ret = classifier_->GetAttributeCategory(classify_result->targets);
            if (ret != util::ErrorEnum::Success) {
                LOG_WARN("{}[{} {} {}] GetAttributeCategory Failed", log_tag_, GetTaskId(), GetName(), uuid);
            }
        }
    }

    for (size_t i = 0; i < alg_out_datas.size(); i++) {
        // Pass-through: copy classify results back to the original data type slot.
        if (AlgDataType::ChannelDataDetect == alg_out_datas[i]->dataType) {
            alg_out_datas[i]->chanDataDetect.detRet =
                alg_out_datas[i]->GetTaskResult(AlgDataType::TaskDataClassify);
        } else if (AlgDataType::TaskDataClassify == alg_out_datas[i]->dataType) {
            // No action needed for classify type.
        } else if (AlgDataType::TaskDataLandmark == alg_out_datas[i]->dataType) {
            alg_out_datas[i]->SetTaskResult(AlgDataType::TaskDataLandmark,
                                            alg_out_datas[i]->GetTaskResult(AlgDataType::TaskDataClassify));
        } else {
            alg_out_datas[i]->SetTaskResult(AlgDataType::TaskDataTrack,
                                            alg_out_datas[i]->GetTaskResult(AlgDataType::TaskDataClassify));
        }
        RecordHistory(alg_out_datas[i]);
        distributor->DistributorData(alg_out_datas[i]);
    }
}

}  // namespace cosmo
