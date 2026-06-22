// AI area-based classifier action implementation.
// Classifies targets within configured detection areas.

#include "flow/classify/AiClassifierArea.h"

#include "flow/common/AlgDataRecord.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

namespace cosmo {

AiClassifierArea::AiClassifierArea(const std::string& taskId, ActionNode& action)
    : AiClassifierBase(AlgActionType::AlgActionAiClassifyArea, taskId, action, "AI-CLASSIFIER-AREA ",
                       "classify_" + action.flowActionId) {
    batch_count_ = 1;
    for (auto& el : action.configObject.params) {
        if (key::INPUT_AREA_TYPE == el.key.ToString()) {
            auto value = util::ParseInt(el.value);
            if (IsValidInputAreaType(value)) {
                params_.input_area_type = static_cast<MsgInputAreaType>(util::ParseInt(el.value));
                LOG_INFO("{}[{} {} {}] Set {} To {} ", log_tag_, GetTaskId(), GetName(), uuid, el.key,
                         el.value);
            } else {
                LOG_WARN("{}[{} {} {}] Set {} To {} Failed", log_tag_, GetTaskId(), GetName(), uuid, el.key,
                         el.value);
            }
        }
    }
    LOG_INFO("{}[{} {} {}] Init ", log_tag_, GetTaskId(), GetName(), uuid);
}

// Set areas - clears previous areas and applies a full replacement.
bool AiClassifierArea::SetArea(const std::string& /*channelId*/, const std::string& /*taskId*/,
                               std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& /*shieldedAreas*/) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    task_areas_ = areas;
    return true;
}

std::vector<MsgTaskArea> AiClassifierArea::GetAssoAreas(std::vector<MsgTaskArea> areas) {
    std::vector<MsgTaskArea> asso_area = areas;
    for (auto& area : areas) {
        if (area.bHaveAssoArea) {
            auto asso_unit = GetAssoAreas(area.associatedAreas);
            asso_area.insert(asso_area.end(), asso_unit.begin(), asso_unit.end());
        }
    }
    return asso_area;
}

std::vector<MsgTaskArea> AiClassifierArea::GetAreas() {
    std::vector<MsgTaskArea> ret_areas;
    std::shared_lock<std::shared_mutex> lock(mtx);
    if (MsgInputAreaType::Main == params_.input_area_type) {
        ret_areas = task_areas_;
        return ret_areas;
    } else {
        for (auto& area : task_areas_) {
            if (area.bHaveAssoArea) {
                auto asso_area_unit = GetAssoAreas(area.associatedAreas);
                ret_areas.insert(ret_areas.end(), asso_area_unit.begin(), asso_area_unit.end());
            }
        }

        if (MsgInputAreaType::All == params_.input_area_type) {
            ret_areas.insert(ret_areas.end(), task_areas_.begin(), task_areas_.end());
        }
    }

    return ret_areas;
}

std::vector<AiDetectRstEl> AiClassifierArea::GenTargets() {
    std::vector<AiDetectRstEl> targets;
    auto areas = GetAreas();
    for (auto& area : areas) {
        AiDetectRstEl target;
        target.box.x      = static_cast<int>(width_ * area.pointBox.x);
        target.box.y      = static_cast<int>(height_ * area.pointBox.y);
        target.box.width  = static_cast<int>(width_ * area.pointBox.width);
        target.box.height = static_cast<int>(height_ * area.pointBox.height);

        TargetAreaUnit area_unit;
        area_unit.area_id   = area.areaId;
        area_unit.area_name = area.name;
        target.areaSign.areas.push_back(area_unit);

        targets.push_back(target);
    }

    return targets;
}

bool AiClassifierArea::CheckDataAvailable(AlgDataPtr algData) {
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

    // Validate that the decoded frame is available.
    if (!algData->chanDataDec.frame) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{} {} {}] Filter {} Frames dataType:{}", log_tag_, GetTaskId(), GetName(), uuid,
                     invalid_frame_cnt, algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return false;
    }

    width_  = algData->chanDataDec.frame->GetWidth();
    height_ = algData->chanDataDec.frame->GetHeight();
    return true;
}

void AiClassifierArea::HandFramesEx(std::vector<AlgDataPtr> alg_datas) {
    std::vector<VideoFramePtr> in_images;
    std::vector<AiDetectRstEl> io_puts;
    std::vector<int> img_record;
    std::vector<AlgDataPtr> alg_out_datas;

    size_t image_num = alg_datas.size();
    for (size_t i = 0; i < image_num; i++) {
        auto alg_data               = alg_datas[i];
        AlgDataPtr alg_out_data     = AlgDataCopy(alg_data);
        alg_out_data->dataType      = AlgDataType::TaskDataClassify;
        alg_out_data->bHaveClassify = true;
        auto result                 = std::make_shared<DataDetTrackClassify>();
        alg_out_data->SetTaskResult(AlgDataType::TaskDataClassify, result);
        if (alg_data->chanDataDetect.detRet) {
            result->frameIndex        = alg_data->chanDataDetect.detRet->frameIndex;
            result->streamIndex       = alg_data->chanDataDetect.detRet->streamIndex;
            result->timestamp         = alg_data->chanDataDetect.detRet->timestamp;
            result->picWidth          = alg_data->chanDataDetect.detRet->picWidth;
            result->picHeight         = alg_data->chanDataDetect.detRet->picHeight;
            result->bHaveArea         = alg_data->chanDataDetect.detRet->bHaveArea;
            result->bHaveShieldedArea = alg_data->chanDataDetect.detRet->bHaveShieldedArea;
        } else {
            result->frameIndex =
                alg_data->chanDataDec.frame ? alg_data->chanDataDec.frame->GetFrameIndex() : 0;
            result->streamIndex =
                alg_data->chanDataDec.frame ? alg_data->chanDataDec.frame->GetStreamIndex() : 0;
            result->timestamp = alg_data->chanDataDec.frame ? alg_data->chanDataDec.frame->GetTimestamp() : 0;
            result->picWidth  = alg_data->chanDataDec.frame ? alg_data->chanDataDec.frame->GetWidth()
                                                            : media::kVideoDefaultWidth;
            result->picHeight = alg_data->chanDataDec.frame ? alg_data->chanDataDec.frame->GetHeight()
                                                            : media::kVideoDefaultHeight;
        }
        result->bHaveArea         = true;
        result->bHaveShieldedArea = false;
        alg_out_datas.push_back(alg_out_data);
        // Generate area-based targets
        auto gen_targets = GenTargets();

        size_t box_nums = gen_targets.size();
        for (size_t j = 0; j < box_nums; j++) {
            in_images.push_back(alg_data->chanDataDec.frame);
            // Compose classification targets
            io_puts.push_back(gen_targets[j]);
            img_record.push_back(i);

            if ((in_images.size() == batch_count_) || ((i == (image_num - 1)) && (j == (box_nums - 1)))) {
                action_status = classifier_->Classify(in_images, io_puts);
                for (size_t out = 0; out < io_puts.size(); out++) {
                    alg_out_datas[img_record[out]]
                        ->GetTaskResult(AlgDataType::TaskDataClassify)
                        ->targets.push_back(io_puts[out]);
                }
                in_images.clear();
                io_puts.clear();
                img_record.clear();
            }
        }
    }

    if (!in_images.empty()) {
        action_status = classifier_->Classify(in_images, io_puts);
        for (size_t out = 0; out < io_puts.size(); out++) {
            alg_out_datas[img_record[out]]
                ->GetTaskResult(AlgDataType::TaskDataClassify)
                ->targets.push_back(io_puts[out]);
        }
        in_images.clear();
        io_puts.clear();
        img_record.clear();
    }

    for (size_t i = 0; i < alg_out_datas.size(); i++) {
        RecordHistory(alg_out_datas[i]);
        distributor->DistributorData(alg_out_datas[i]);
    }
}

}  // namespace cosmo