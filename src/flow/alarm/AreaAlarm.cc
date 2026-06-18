// AreaAlarm — Area alarm dispatch (core scheduling)

#include "flow/alarm/AreaAlarm.h"

#include <algorithm>

#include "flow/alarm/AreaAlarmInternalTypes.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"

static constexpr const char* kTag = "AreaAlarm ";

namespace chrono = std::chrono;
namespace cosmo {

AreaAlarm::~AreaAlarm() {
    LOG_INFO("{}Task:{} {}/{} Stop", kTag, task_id, action_info_.actionName, action_info_.flowActionId);
    Stop();
    LOG_INFO("{}Task:{} {}/{} Delete", kTag, task_id, action_info_.actionName, action_info_.flowActionId);
}

AreaAlarm::AreaAlarm(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBAAreaAlarm, action, "", taskId),
      action_info_(action),
      report_time_point_(chrono::steady_clock::now()) {
    action_status                   = util::ErrorEnum::ActionReady;
    constexpr int kMaxDataQueueSize = 4;
    data_queue->SetMaxSize(kMaxDataQueueSize);
    int value = 0;
    for (auto& el : action.configObject.params) {
        if (2 == el.keys.size()) {
            if (key::area::LIMIT_TARGET_COUNT == el.keys[0]) {
                SetMultTargetAreaLimitTargetCount(el, el.keys[1]);
            } else if (key::area::LIMIT_TARGET_TYPE == el.keys[0]) {
                SetMultTargetAreaLimitTargetType(el, el.keys[1]);
            }
            LOG_INFO("{}Task:{} {}/{} Init {} Set To {}", kTag, task_id, action_info_.actionName,
                     action_info_.flowActionId, el.key, el.value);
            continue;
        }
        if (key::area::ALARM_TYPE == el.key.ToString()) {
            value = util::ParseInt(el.value);
            type_ = static_cast<AreaAlarmType>(value);
            LOG_INFO("{}Task:{} {}/{} Init {} Set To {}", kTag, task_id, action_info_.actionName,
                     action_info_.flowActionId, el.key, el.value);
        } else if ((key::area::BREAK_AREA_TYPE == el.key.ToString()) ||
                   (key::area::COUNT_BREAK_AREA_TYPE == el.key.ToString()) ||
                   (key::area::DETECT_BREAK_AREA_TYPE == el.key.ToString()) ||
                   (key::area::DURATION_BREAK_AREA_TYPE == el.key.ToString())) {
            value            = util::ParseInt(el.value);
            break_area_type_ = static_cast<BreakAreaType>(value);
            LOG_INFO("{}Task:{} {}/{} Init {} Set To {}", kTag, task_id, action_info_.actionName,
                     action_info_.flowActionId, el.key, el.value);
        } else if (key::area::LIMIT_TARGET_COUNT == el.key.ToString()) {
            value                           = util::ParseInt(el.value);
            params_.area_limit_target_count = value;
            LOG_INFO("{}Task:{} {}/{} Init {} Set To {}", kTag, task_id, action_info_.actionName,
                     action_info_.flowActionId, el.key, el.value);
        } else if (key::area::LIMIT_TARGET_TYPE == el.key.ToString()) {
            value                          = util::ParseInt(el.value);
            params_.area_limit_target_type = static_cast<AlgCompareType>(value);
            LOG_INFO("{}Task:{} {}/{} Init {} Set To {}", kTag, task_id, action_info_.actionName,
                     action_info_.flowActionId, el.key, el.value);
        } else if (key::area::DURATION == el.key.ToString()) {
            value                     = util::ParseInt(el.value);
            params_.area_duration_src = value;
            params_.area_duration     = params_.area_duration_src * params_.area_duration_time_type;
            LOG_INFO("{}Task:{} {}/{} Init {} Set To {} areaDuration:{}", kTag, task_id,
                     action_info_.actionName, action_info_.flowActionId, el.key, el.value,
                     params_.area_duration);
        } else if (key::area::DURATION_TIME_TYPE == el.key.ToString()) {
            value                           = util::ParseInt(el.value);
            params_.area_duration_time_type = value;
            params_.area_duration           = params_.area_duration_src * params_.area_duration_time_type;
            LOG_INFO("{}Task:{} {}/{} Init {} Set To {} areaDuration:{}", kTag, task_id,
                     action_info_.actionName, action_info_.flowActionId, el.key, el.value,
                     params_.area_duration);
        } else if (key::INPUT_AREA_TYPE == el.key.ToString()) {
            value = util::ParseInt(el.value);
            if (IsValidInputAreaType(value)) {
                params_.input_area_type = static_cast<MsgInputAreaType>(util::ParseInt(el.value));
                LOG_INFO("{}[{} {}] Set {} To {} ", kTag, GetTaskId(), GetName(), el.key, el.value);
            } else {
                LOG_WARN("{}[{} {}] Set {} To {} Failed", kTag, GetTaskId(), GetName(), el.key, el.value);
            }
        } else if (key::TARGET_COUNT_CHANGE == el.key.ToString()) {
            params_.target_count_change = util::ParseInt(el.value) ? true : false;
            LOG_INFO("{}[{} {}] Set {} To {} ", kTag, GetTaskId(), GetName(), el.key, el.value);
        } else {
            LOG_WARN("{}[{} {}] Set {} To {} Failed", kTag, GetTaskId(), GetName(), el.key, el.value);
        }
    }

    LOG_INFO("{}Task:{} {}/{} Init type:{} breakAreaType:{}", kTag, task_id, action_info_.actionName,
             action_info_.flowActionId, type_, break_area_type_);
}

// --- HandFrame helpers (DEBT-C08 B4) ---

bool AreaAlarm::HasLlmPrejudgedAlarm(const AlgDataPtr& algData) {
    if (!algData->taskDataAlarm.alarmData || algData->taskDataAlarm.alarmData->alarms.empty())
        return false;

    return std::any_of(algData->taskDataAlarm.alarmData->alarms.begin(),
                       algData->taskDataAlarm.alarmData->alarms.end(),
                       [](const auto& alarmUnit) { return alarmUnit.bLlmPrejudged; });
}

bool AreaAlarm::IsSupportedDataType(AlgDataType dataType) {
    return dataType == AlgDataType::ChannelDataDetect || dataType == AlgDataType::TaskDataTrack ||
           dataType == AlgDataType::TaskDataClassify || dataType == AlgDataType::TaskDataFaceLogic ||
           dataType == AlgDataType::TaskDataPersonFace || dataType == AlgDataType::TaskDataAssoTarget ||
           dataType == AlgDataType::TaskDataFriendDistance;
}

DataDetTrackClassifyPtr AreaAlarm::ResolveInputData(const AlgDataPtr& algData) {
    if (AlgDataType::ChannelDataDetect == algData->dataType)
        return algData->chanDataDetect.detRet;
    if (AlgDataType::TaskDataTrack == algData->dataType)
        return algData->GetTaskResult(AlgDataType::TaskDataTrack);
    if (AlgDataType::TaskDataFaceLogic == algData->dataType)
        return algData->GetTaskResult(AlgDataType::TaskDataFaceLogic);
    if (AlgDataType::TaskDataPersonFace == algData->dataType)
        return algData->GetTaskResult(AlgDataType::TaskDataTrack);
    if (AlgDataType::TaskDataAssoTarget == algData->dataType)
        return algData->GetTaskResult(AlgDataType::TaskDataAssoTarget);
    if (AlgDataType::TaskDataFriendDistance == algData->dataType)
        return algData->GetTaskResult(AlgDataType::TaskDataFriendDistance);
    return algData->GetTaskResult(AlgDataType::TaskDataClassify);
}

// --- HandFrame orchestrator ---

void AreaAlarm::HandFrame(AlgDataPtr algData) {
    is_alarm_needed_ = false;
    if (!algData) {
        invalid_frame_cnt += 1;
        constexpr int kInvalidLogInterval = 100;
        if (0 == invalid_frame_cnt % kInvalidLogInterval) {
            LOG_WARN("{}[{}] {}/{} Filter {} Frames", kTag, task_id, action_info_.actionName,
                     action_info_.flowActionId, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    // LLM prejudged alarm bypass — transparent passthrough for Qwen3VL etc.
    if (HasLlmPrejudgedAlarm(algData)) {
        is_alarm_needed_ = true;
        handle_frame_cnt += 1;
        algData->taskDataAlarm.alarmData->multiAlarms += 1;
        action_status = util::ErrorEnum::Success;
        distributor->DistributorData(algData);
        return;
    }

    if (!IsSupportedDataType(algData->dataType)) {
        invalid_frame_cnt += 1;
        constexpr int kInvalidLogInterval = 100;
        if (0 == invalid_frame_cnt % kInvalidLogInterval) {
            LOG_WARN("{}[{}] {}/{} Filter {} Frames dataType:{}", kTag, task_id, action_info_.actionName,
                     action_info_.flowActionId, invalid_frame_cnt, algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    DataDetTrackClassifyPtr input = ResolveInputData(algData);

    if (algData->bHaveTrack) {
        has_track_ = true;
    }

    frame_index_ = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetFrameIndex() : 0;
    if ((AreaAlarmType::kTargetLimit == type_) || (AreaAlarmType::kTargetLimit1 == type_) ||
        (AreaAlarmType::kTargetLimitMultTargets == type_)) {
        HandAreaTargetLimit(algData, input);
    } else if ((AreaAlarmType::kTargetAlarm == type_) || (AreaAlarmType::kInAreaDuration == type_) ||
               (AreaAlarmType::kInArea == type_)) {
        if (AlgDataType::TaskDataFriendDistance == algData->dataType) {
            HandFriendTargetAlarm(algData, input);
        } else {
            HandAreaTargetAlarm(algData, input);
        }
    } else if (AreaAlarmType::kTargetCountReport == type_) {
        HandAreaTargetCount(algData, input);
    } else if (AreaAlarmType::kDirection == type_) {
        HandWrongDirection(algData, input);
    }

    handle_frame_cnt += 1;
    constexpr int kHandleLogInterval = 500;
    if (0 == handle_frame_cnt % kHandleLogInterval) {
        LOG_INFO("{}[{}] {}/{} Handle {} Frames", kTag, task_id, action_info_.actionName,
                 action_info_.flowActionId, handle_frame_cnt);
    }

    // Dispatch alarm to downstream modules when triggered
    if ((algData->taskDataAlarm.alarmData) && (is_alarm_needed_)) {
        algData->taskDataAlarm.alarmData->multiAlarms += 1;
        action_status = util::ErrorEnum::Success;
        distributor->DistributorData(algData);
    }
}

void AreaAlarm::FillAlarmData(AlgDataPtr algData, DataAlarmUnit& alarmUnit) {
    if (!algData->taskDataAlarm.alarmData) {
        algData->taskDataAlarm.alarmData               = std::make_shared<DataAlarm>();
        algData->taskDataAlarm.alarmData->flowActionId = action_info_.flowActionId;
    }

    auto alarmData = algData->taskDataAlarm.alarmData;
    if (!alarmData) {
        LOG_WARN("{}[{}] {}/{} NEED ALARM But No MEM", kTag, task_id, action_info_.actionName,
                 action_info_.flowActionId);
        return;
    }

    is_alarm_needed_ = true;
    alarmData->alarms.push_back(alarmUnit);
}

void AreaAlarm::FillAlarmDataCombine(AlgDataPtr algData, DataAlarmUnit& alarmUnit) {
    auto it = std::find_if(alarm_data_->alarms.begin(), alarm_data_->alarms.end(),
                           [&alarmUnit](const auto& alarm) { return alarm.areaId == alarmUnit.areaId; });
    if (it != alarm_data_->alarms.end()) {
        alarmUnit.boxs.insert(alarmUnit.boxs.end(), it->boxs.begin(), it->boxs.end());
        is_alarm_needed_ = true;
        algData->taskDataAlarm.alarmData->alarms.push_back(alarmUnit);
    }
}

}  // namespace cosmo