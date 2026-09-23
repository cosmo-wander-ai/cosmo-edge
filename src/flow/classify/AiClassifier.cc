// AI classifier action implementation.
// Consumes data via registered processing queues.

#include "flow/classify/AiClassifier.h"

#include <algorithm>

#include "flow/common/AlgDataRecord.h"
#include "flow/common/FlowTaskUtil.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "util/Log.h"

namespace cosmo {

AiClassifier::AiClassifier(const std::string& taskId, ActionNode& action)
    : AiClassifierBase(AlgActionType::AlgActionAiClassify, taskId, action, "AI-CLASSIFIER ",
                       "classify_" + action.atomicCode) {
    batch_count_ = 1;
    LOG_INFO("{}[{} {} {}] Init ", log_tag_, GetTaskId(), GetName(), uuid);
}

bool AiClassifier::CheckDataAvailable(AlgDataPtr algData) {
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
        if (!AiSdkInit() && !algData->bHaveRelated) {
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
          (AlgDataType::TaskDataAssoTarget == algData->dataType) ||
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

void AiClassifier::HandFramesEx(std::vector<AlgDataPtr> alg_datas) {
    std::vector<AlgDataPtr> alg_out_datas;
    std::vector<VideoFramePtr> in_images;
    std::vector<media::NativeVideoBufferPtr> in_native_buffers;
    std::vector<std::vector<AiDetectRstEl>> io_puts;
    std::vector<int> img_record;
    size_t image_num              = alg_datas.size();
    bool target_have_mult_related = false;
    for (size_t i = 0; i < image_num; i++) {
        auto alg_data               = alg_datas[i];
        AlgDataPtr alg_out_data     = AlgDataCopy(alg_data);
        alg_out_data->dataType      = AlgDataType::TaskDataClassify;
        alg_out_data->bHaveClassify = true;
        auto classify_result        = std::make_shared<DataDetTrackClassify>();
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
        } else if (AlgDataType::TaskDataPersonFace == alg_data->dataType ||
                   AlgDataType::TaskDataAssoTarget == alg_data->dataType) {
            input = alg_data->GetTaskResult(alg_data->dataType);
        }

        if (input) {
            classify_result->bHaveArea            = input->bHaveArea;
            classify_result->bHaveShieldedArea    = input->bHaveShieldedArea;
            classify_result->observation_complete = input->observation_complete;
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
        size_t box_nums          = targets.size();
        target_have_mult_related = input ? input->targetHaveMultRelated : false;
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
        in_native_buffers.push_back(alg_data->chanDataDec.native_buffer);
        img_record.push_back(i);
    }

    action_status = util::ErrorEnum::Success;
    // Keep only this invocation's outputs: models may be reused by several nodes.
    const auto before_classification = io_puts;
    if (!io_puts.empty()) {
        action_status = classifier_
                            ? classifier_->ClassifyMultSub(in_images, io_puts, target_have_mult_related, true,
                                                           in_native_buffers)
                            : util::ErrorEnum::AI_INST_NOTCREATED;
    }
    for (size_t out = 0; out < io_puts.size(); out++) {
        auto classify_result = alg_out_datas[img_record[out]]->GetTaskResult(AlgDataType::TaskDataClassify);
        if (classify_result) {
            classify_result->observation_complete =
                classify_result->observation_complete && action_status == util::ErrorEnum::Success;
            for (size_t target_index = 0; target_index < io_puts[out].size(); ++target_index) {
                auto& el              = io_puts[out][target_index];
                auto& observation     = el.classificationObservations[GetFlowActionId()];
                observation.modelCode = alg_code_;
                observation.failed    = action_status != util::ErrorEnum::Success;
                observation.results.clear();
                // MultSub may prepend its output; compare source-tagged multisets
                // instead of assuming the insertion position.
                auto previous = before_classification[out][target_index].classifyRst;
                if (!observation.failed) {
                    for (const auto& value : el.classifyRst) {
                        auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& item) {
                            return item.atomic_code == value.atomic_code && item.label == value.label &&
                                   item.confidence == value.confidence;
                        });
                        if (old != previous.end())
                            previous.erase(old);
                        else if (value.atomic_code == alg_code_)
                            observation.results.push_back(value);
                    }
                }
                classify_result->targets.push_back(el);
            }
        }
    }

    for (size_t i = 0; i < alg_out_datas.size(); i++) {
        RecordHistory(alg_out_datas[i]);
        distributor->DistributorData(alg_out_datas[i]);
    }
}

}  // namespace cosmo
