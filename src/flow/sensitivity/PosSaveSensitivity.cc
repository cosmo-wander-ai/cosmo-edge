// Sensitivity calculation class

#include "flow/sensitivity/PosSaveSensitivity.h"

#include "flow/common/AlgDataRecord.h"
#include "flow/sensitivity/PosSaveSensitivityTypes.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag = "PosSaveSensitivity ";
namespace cosmo {
// TrackIdData — defined in PosSaveSensitivityTypes.h

PosSaveSensitivity::~PosSaveSensitivity() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

PosSaveSensitivity::PosSaveSensitivity(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBAPosSaveSensitivity, action, "", taskId),
      action_info_(action),
      overview_rec_inst_(taskId, "PosSaveSen_" + action.flowActionId) {
    action_status = util::ErrorEnum::ActionReady;
    for (auto& el : action.configObject.params) {
        if (key::pos_sen::REAL_TIME_ENABLE == el.key.ToString()) {
            auto value                       = util::ParseInt(el.value);
            params_.pos_sen_real_time_enable = value;
            LOG_INFO("{}[{} {}] Set {} To {} ", kTag, GetTaskId(), GetName(), el.key, el.value);
        }
    }
    LOG_INFO("{}Task:{} Init", kTag, task_id);
}

/*
param.posSenDurationTime
param.posSenDurationTimeType
param.posSenHitCount
param.posSenTotalCount
*/
bool PosSaveSensitivity::AnalysisKey(MsgDynamicKeyValue& param, BAPosSaveSensitivityParam& localParamEl) {
    if (param.keys.empty()) {
        LOG_WARN(
            "ModifyParam "
            "[{}] param.keys is Empty",
            task_id);
        return false;
    }
    if (param.keys.size() != 2) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] Set {} Failed. key size:{}",
            task_id, param.key, param.keys.size());
        return false;
    }

    if (key::PARAM != param.keys[0]) {
        LOG_DEBUG(
            "ModifyParam "
            "[{}] param.keys[0] is Not {}",
            task_id, key::PARAM);
        return false;
    }

    if (param.keys[1] == key::pos_sen::DURATION_TIME) {
        auto value = util::ParseInt<long long>(param.value);
        if (value < 0) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.pos_sen_duration_time = value;
        localParamEl.pos_sen_duration =
            localParamEl.pos_sen_duration_time * localParamEl.pos_sen_duration_time_type;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set param.key:{} param.value:{}",
            task_id, param.key, param.value);
    } else if (param.keys[1] == key::pos_sen::DURATION_TIME_TYPE) {
        auto value = util::ParseInt<long long>(param.value);
        if (value < 0) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.pos_sen_duration_time_type = value;
        localParamEl.pos_sen_duration =
            localParamEl.pos_sen_duration_time * localParamEl.pos_sen_duration_time_type;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set param.key:{} param.value:{}",
            task_id, param.key, param.value);
    } else if (param.keys[1] == key::pos_sen::HIT_COUNT) {
        auto value = util::ParseInt<long long>(param.value);
        if (value < 1) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.pos_sen_hit_count = value;
        if (localParamEl.pos_sen_total_count > 0) {
            localParamEl.sensitivity_ratio = static_cast<float>(
                static_cast<double>(localParamEl.pos_sen_hit_count) / localParamEl.pos_sen_total_count);
        }
        LOG_INFO(
            "ModifyParam "
            "[{}] Set param.key:{} param.value:{} sensitivityRatio:{}",
            task_id, param.key, param.value, localParamEl.sensitivity_ratio);
    } else if (param.keys[1] == key::pos_sen::TOTAL_COUNT) {
        auto value = util::ParseInt<long long>(param.value);
        if (value < 1) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.pos_sen_total_count = value;
        localParamEl.sensitivity_ratio   = static_cast<float>(
            static_cast<double>(localParamEl.pos_sen_hit_count) / localParamEl.pos_sen_total_count);
        LOG_INFO(
            "ModifyParam "
            "[{}] param.key:{} param.value:{} sensitivityRatio:{}",
            task_id, param.key, param.value, localParamEl.sensitivity_ratio);
    }

    else {
        return false;
    }

    return true;
}

// Modify parameters based on existing ones
bool PosSaveSensitivity::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                     std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        AnalysisKey(param, params_);
    }
    LOG_INFO(
        "ModifyParam "
        "[{}] Set durationMs:{} sensitivityRatio:{} posSenHitCount:{} posSenTotalCount:{}",
        task_id, params_.pos_sen_duration, params_.sensitivity_ratio, params_.pos_sen_hit_count,
        params_.pos_sen_total_count);
    return false;
}

// Set parameters - clear previous ones and set fully new ones
bool PosSaveSensitivity::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                  std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear parameters first
    params_ = {};
    for (auto& param : params) {
        AnalysisKey(param, params_);
    }
    LOG_INFO(
        "ModifyParam "
        "[{}] Set durationMs:{} sensitivityRatio:{} posSenHitCount:{} posSenTotalCount:{}",
        task_id, params_.pos_sen_duration, params_.sensitivity_ratio, params_.pos_sen_hit_count,
        params_.pos_sen_total_count);
    return false;
}

// Set areas - clear previous ones and set fully new ones
bool PosSaveSensitivity::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                                 std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    task_area_.taskId        = taskId;
    task_area_.areas         = areas;
    task_area_.shieldedAreas = shieldedAreas;
    for (auto& area : task_area_.areas) {
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& assArea : area.associatedAreas) {
            assArea.iderectionType = GetDirectionTypeFromMsg(assArea.params);
        }
    }
    return true;
}

[[nodiscard]] TaskBaseArea PosSaveSensitivity::GetArea() {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto area = task_area_;
    return area;
}

// Associate target
void PosSaveSensitivity::AssoTarget(AlgDataPtr algData, TrackIdData& idData) {
    // No need to update if detected
    if (idData.is_behavior_detected) {
        return;
    }

    // Frame data
    //	idData.frame = algData->chanDataDec.frame;
    idData.group_targets.clear();
    auto input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
    // No track data, no need to associate
    if (!input) {
        return;
    }
    for (auto& trackId : idData.target.groupTargets) {
        for (auto& target : input->targets) {
            if (trackId == target.trackId) {
                idData.group_targets.push_back(target);
            }
        }
    }
}

void PosSaveSensitivity::AddHistory(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    for (auto& target : input->targets) {
        auto& idData = map_track_id_status_[target.trackId];
        if (idData.track_id_uuid.empty())  // Appears for the first time
        {
            idData.track_id_uuid   = target.trackIdInfo;
            idData.first_timestamp = timestamp_;
            LOG_AITARGET("{}[{}] TrackId {} Appear", kTag, task_id, target.trackId);
        }
        idData.track_id       = target.trackId;
        idData.is_track_exist = true;
        idData.target         = target;
        idData.last_timestamp = timestamp_;
        // Not filtered, performed logic calculation
        if (!target.bFilter) {
            idData.logic_count += 1;
        }

        AssoTarget(algData, idData);
        TrackIdData::TrackIdDataEl trackData;

        trackData.logic_result                     = target.bLogicResult;
        trackData.is_filtered                      = target.bFilter;
        trackData.filter_desc                      = target.filterDesc;
        trackData.target_confidence_info.box       = target.box;
        trackData.target_confidence_info.targetPos = target.targetPos;

        // Area & Shild Area
        trackData.has_area          = input->bHaveArea;
        trackData.has_shielded_area = input->bHaveShieldedArea;
        for (auto& shiledArea : target.areaSign.shielded_areas) {
            trackData.shield_areas.push_back({shiledArea.area_id, shiledArea.area_name});
        }
        for (auto& targetAreas : target.areaSign.areas) {
            trackData.areas.push_back({targetAreas.area_id, targetAreas.area_name});
        }

        idData.history.push_back(trackData);
    }
}

void PosSaveSensitivity::OldHistory() {
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end();) {
        while ((it->second.history.size() > params_.pos_sen_total_count)) {
            it->second.history.pop_front();
        }
        it->second.is_history_full = !it->second.history.empty();
        ++it;
    }
}

void PosSaveSensitivity::TrackData2RecData(TrackIdData& idData, MsgRecPosSaveSensitityTarget& recTarget) {
    recTarget.trackId          = idData.track_id;
    recTarget.behaviorDetected = idData.is_behavior_detected;
    recTarget.duration         = idData.last_timestamp - idData.first_timestamp;
    recTarget.aiBox.x          = idData.target.box.x;
    recTarget.aiBox.y          = idData.target.box.y;
    recTarget.aiBox.width      = idData.target.box.width;
    recTarget.aiBox.height     = idData.target.box.height;
    if ((width_ > 0) && (height_ > 0)) {
        recTarget.box.x      = static_cast<double>(recTarget.aiBox.x) / width_;
        recTarget.box.y      = static_cast<double>(recTarget.aiBox.y) / height_;
        recTarget.box.width  = static_cast<double>(recTarget.aiBox.width) / width_;
        recTarget.box.height = static_cast<double>(recTarget.aiBox.height) / height_;
    }
}

// sensitivity calculation — moved to PosSaveSensitivityCalc.cc

}  // namespace cosmo
