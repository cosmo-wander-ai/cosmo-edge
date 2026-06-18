// AI group-based classifier action implementation.
// Groups targets by area and classifies the composite bounding box.

#include "flow/classify/AiClassifierGroup.h"

#include "flow/common/AlgDataRecord.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelPathMapping.h"
#include "service/model/IModelService.h"
#include "service/system/IConfigReadService.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/UuidUtil.h"

namespace cosmo {

AiClassifierGroup::AiClassifierGroup(const std::string& taskId, ActionNode& action)
    : AiClassifierBase(AlgActionType::AlgActionAiClassifyGroup, taskId, action, "AI-CLASSIFIER-GROUP ",
                       "classify_" + action.flowActionId) {
    batch_count_ = 2;

    for (auto& el : action.configObject.params) {
        if (key::group::TYPE == el.key.ToString()) {
            auto value = util::ParseInt(el.value);
            if (ValidateClassifyGroupType(value)) {
                params_.group_type = static_cast<AlgClassifyGroupType>(value);
                LOG_INFO("{}[{} {} {}] Init {} Set To {}", log_tag_, GetTaskId(), GetName(), uuid, el.key,
                         el.value);
            }
        } else if (key::group::AREA_TARGET_COUNT == el.key.ToString()) {
            auto value = util::ParseInt(el.value);
            if (value >= 0) {
                params_.group_area_target_count = value;
                LOG_INFO("{}[{} {} {}] Init {} Set To {}", log_tag_, GetTaskId(), GetName(), uuid, el.key,
                         el.value);
            }
        }
    }
    LOG_INFO("{}[{} {} {}] Init ", log_tag_, GetTaskId(), GetName(), uuid);
}

void AiClassifierGroup::SetAssoAreas(std::vector<MsgTaskArea>& areas) {
    for (auto& area : areas) {
        GroupAreaData area_data;
        area_data.area_id   = area.areaId;
        area_data.area_name = area.name;
        SetAssoAreas(area.associatedAreas);
        group_area_datas_.push_back(area_data);
    }
}

// Set areas - clears previous areas and applies a full replacement.
bool AiClassifierGroup::SetArea(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                std::vector<MsgTaskArea>& areas,
                                std::vector<MsgTaskArea>& /*shieldedAreas*/) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    group_area_datas_.clear();
    for (auto& area : areas) {
        GroupAreaData area_data;
        area_data.area_id   = area.areaId;
        area_data.area_name = area.name;
        SetAssoAreas(area.associatedAreas);
        group_area_datas_.push_back(area_data);
    }
    return true;
}

bool AiClassifierGroup::GenGroupEl(AiGroupEl& el) {
    size_t group_area_target_count = static_cast<size_t>(params_.group_area_target_count);
    // Target count does not meet the requirement.
    if (el.srcTargets.size() != group_area_target_count) {
        el.genTarget.bFilter    = true;
        el.genTarget.filterDesc = "Filter By targetCount unequal with param";
        el.genTarget.filterType = AIFilterType::TargetCount;
        return false;
    }

    // Record grouped target track IDs.
    for (auto& src_target : el.srcTargets) {
        el.genTarget.groupTargets.push_back(src_target.trackId);
    }

    // Target count satisfies the requirement; compute bounding box.
    constexpr int kMinCoordSentinel = 99999999;
    int min_x                       = kMinCoordSentinel;
    int min_y                       = kMinCoordSentinel;
    int max_x                       = 0;
    int max_y                       = 0;
    // Find top-left and bottom-right corners.
    for (auto& target : el.srcTargets) {
        if (min_x > target.box.x) {
            min_x = target.box.x;
        }
        if (min_y > target.box.y) {
            min_y = target.box.y;
        }

        if (max_x < target.box.x + target.box.width) {
            max_x = target.box.x + target.box.width;
        }

        if (max_y < target.box.y + target.box.height) {
            max_y = target.box.y + target.box.height;
        }
    }

    if ((min_x < max_x) && (min_y < max_y)) {
        el.genTarget.box.x      = min_x;
        el.genTarget.box.y      = min_y;
        el.genTarget.box.width  = max_x - min_x;
        el.genTarget.box.height = max_y - min_y;
        return true;
    }
    return false;
}

std::vector<AiGroupEl> AiClassifierGroup::GenGroupEmements() {
    std::vector<AiGroupEl> group_targets;
    for (auto& area_data : group_area_datas_) {
        AiGroupEl el;
        // Check if current and previous targets differ.
        auto sorted_new = area_data.targets;
        auto sorted_old = area_data.last_targets;
        std::sort(sorted_new.begin(), sorted_new.end());
        std::sort(sorted_old.begin(), sorted_old.end());
        if (sorted_new != sorted_old) {
            // Targets changed; assign a new group ID.
            area_data.group_id        = group_id_++;
            area_data.group_id_string = util::GenerateUUID();
        }
        area_data.last_targets = area_data.targets;

        // Assign group ID and source targets.
        el.groupId               = area_data.group_id;
        el.groupIdInfo           = area_data.group_id_string;
        el.srcTargets            = area_data.real_targets;
        el.genTarget.trackId     = el.groupId;
        el.genTarget.trackIdInfo = el.groupIdInfo;

        // Set target area information.
        TargetAreaUnit area;
        area.area_id   = area_data.area_id;
        area.area_name = area_data.area_name;
        el.genTarget.areaSign.areas.push_back(area);
        // Generate the composite group target.
        if (GenGroupEl(el)) {
            group_targets.push_back(el);
        }
    }

    return group_targets;
}

// Populate group_area_datas_ with targets from each area.
void AiClassifierGroup::GenAreaSource(DataDetTrackClassifyPtr input) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    for (auto& area_data : group_area_datas_) {
        // Clear targets for each area.
        area_data.targets.clear();
        area_data.real_targets.clear();

        for (auto& target : input->targets) {
            if (target.bFilter) {
                continue;
            }
            if (!target.areaSign.shielded_areas.empty()) {
                continue;
            }
            // Collect targets belonging to this area.
            if (areaIdInAreaUnits(target.areaSign.areas, area_data.area_id)) {
                area_data.targets.push_back(target.trackId);
                area_data.real_targets.push_back(target);
            }
        }
    }
}

bool AiClassifierGroup::CheckDataAvailable(AlgDataPtr algData) {
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

    // Only accept tracked data for group classification.
    if (!(AlgDataType::TaskDataTrack == algData->dataType)) {
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

void AiClassifierGroup::HandFramesEx(std::vector<AlgDataPtr> alg_datas) {
    std::vector<VideoFramePtr> in_images;
    std::vector<AiDetectRstEl> io_puts;
    std::vector<int> img_record;
    std::vector<AlgDataPtr> alg_out_datas;

    size_t image_num = alg_datas.size();
    for (size_t i = 0; i < image_num; i++) {
        auto alg_data               = alg_datas[i];
        AlgDataPtr alg_out_data     = AlgDataCopy(alg_data);
        alg_out_data->dataType      = AlgDataType::TaskDataGroupClassify;
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
        }

        auto input = alg_data->GetTaskResult(AlgDataType::TaskDataTrack);
        if (AlgDataType::ChannelDataDetect == alg_data->dataType) {
            input = alg_data->chanDataDetect.detRet;
        } else if (AlgDataType::TaskDataClassify == alg_data->dataType) {
            input = alg_data->GetTaskResult(AlgDataType::TaskDataClassify);
        } else if (AlgDataType::TaskDataLandmark == alg_data->dataType) {
            input = alg_data->GetTaskResult(AlgDataType::TaskDataLandmark);
        }

        result->bHaveArea         = input->bHaveArea;
        result->bHaveShieldedArea = input->bHaveShieldedArea;
        alg_out_datas.push_back(alg_out_data);
        // Populate area-based target data.
        GenAreaSource(input);

        // Generate group classification targets.
        auto group_targets   = GenGroupEmements();
        result->groupTargets = group_targets;
        size_t box_nums      = group_targets.size();
        for (size_t j = 0; j < box_nums; j++) {
            in_images.push_back(alg_data->chanDataDec.frame);
            // Compose classification targets.
            io_puts.push_back(group_targets[j].genTarget);
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