// Sensitivity calculation class

#include "flow/sensitivity/Sensitivity.h"

#include <thread>

#include "flow/common/AlgDataRecord.h"
#include "util/Keys.h"
#include "util/Log.h"
#include "util/SafeParse.h"
#include "util/TimeUtil.h"

static constexpr const char* kTag = "Sensitivity ";
namespace cosmo {
#include "flow/sensitivity/SensitivityTypes.h"

Sensitivity::Sensitivity(const std::string& taskId, ActionNode& action)
    : AlgActionBase(AlgActionType::AlgActionBASensitivity, action, "", taskId),
      m_overviewRecInst(taskId, "Sensitivity") {
    LOG_INFO("{}Task:{} Init", kTag, task_id);
}

Sensitivity::~Sensitivity() {
    LOG_INFO("{}Task:{} Stop", kTag, task_id);
    Stop();
    LOG_INFO("{}Task:{} Delete", kTag, task_id);
}

/*
param.sensitivity
param.detectionDurationMs
param.filterToDen
*/
bool Sensitivity::AnalysisKey(MsgDynamicKeyValue& param, BASensitivityParam& localParamEl) {
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

    if (param.keys[1] == key::SENSITIVITY) {
        auto sensitivity = util::ParseInt(param.value);
        if ((sensitivity < 1) || (sensitivity > 10)) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.sensitivity      = sensitivity;
        localParamEl.sensitivityRatio = (11.f - sensitivity) / 10.f;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set sensitivity:{} localParamEl.sensitivityRatio:{} param.key:{} param.value:{}",
            task_id, localParamEl.sensitivity, localParamEl.sensitivityRatio, param.key, param.value);
    } else if (param.keys[1] == key::DETECTION_DURATION_MS) {
        auto durationMs = util::ParseInt<long long>(param.value);
        if ((durationMs < 1) || (durationMs > 100000000000)) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.durationMs    = durationMs;
        float minFps               = (action_alg ? action_alg->algorithmMinFps : m_taskActionsMinFps);
        localParamEl.trackDetCount = static_cast<int64_t>(minFps * durationMs / 1000.f);
        if (localParamEl.trackDetCount > 0) {
            localParamEl.trackDetCount += 1;
        }
        localParamEl.durationMsMax = localParamEl.durationMs + 300;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set durationMs:{} param.key:{} param.value:{} trackDetCount:{}",
            task_id, localParamEl.durationMs, param.key, param.value, localParamEl.trackDetCount);
    } else if (param.keys[1] == key::DETECTION_DURATION) {
        auto duration = util::ParseInt<long long>(param.value);
        if ((duration < 1) || (duration > 100000000)) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.durationMs    = duration * 1000;
        float minFps               = (action_alg ? action_alg->algorithmMinFps : m_taskActionsMinFps);
        localParamEl.trackDetCount = static_cast<int64_t>(duration * minFps);
        if (localParamEl.trackDetCount > 0) {
            localParamEl.trackDetCount += 1;
        }
        localParamEl.durationMsMax = localParamEl.durationMs + 300;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set durationMs:{} param.key:{} param.value:{} trackDetCount:{}",
            task_id, localParamEl.durationMs, param.key, param.value, localParamEl.trackDetCount);
    } else if (param.keys[1] == key::SEN_HIT_COUNT) {
        auto value = util::ParseInt<long long>(param.value);
        if (value < 1) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.senHitCount = value;
        if (localParamEl.senTotalCount > 0) {
            localParamEl.sensitivityRatio = static_cast<float>(static_cast<double>(localParamEl.senHitCount) /
                                                               localParamEl.senTotalCount);
        }
        localParamEl.sensitivityType = SensitivityType::kFixCount;
        LOG_INFO(
            "ModifyParam "
            "[{}] Set param.key:{} param.value:{} senHitCount:{}",
            task_id, param.key, param.value, localParamEl.senHitCount);
    } else if (param.keys[1] == key::SEN_TOTAL_COUNT) {
        auto value = util::ParseInt<long long>(param.value);
        if (value < 1) {
            LOG_WARN(
                "ModifyParam "
                "[{}] param.key {} value {} is Illegal",
                task_id, param.key, param.value);
            return false;
        }
        localParamEl.senTotalCount = value;
        localParamEl.sensitivityRatio =
            static_cast<float>(static_cast<double>(localParamEl.senHitCount) / localParamEl.senTotalCount);
        localParamEl.sensitivityType = SensitivityType::kFixCount;
        LOG_INFO(
            "ModifyParam "
            "[{}] param.key:{} param.value:{} senTotalCount:{}",
            task_id, param.key, param.value, localParamEl.senTotalCount);
    }
    // Remove this parameter, default is to add to denominator
    // else if (param.keys[1] == key::FILTER_TO_DEN)
    // {
    // 	auto filterToDenominator = util::ParseInt(param.value);
    // 	localParamEl.filterToDenominator = (filterToDenominator != 0);
    // }
    else {
        return false;
    }

    return true;
}

// Modify parameters based on existing ones
bool Sensitivity::ModifyParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                              std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    for (auto& param : params) {
        AnalysisKey(param, m_params);
    }
    LOG_INFO(
        "ModifyParam "
        "[{}] Set sensitivityType:{} durationMs:{}-{} trackDetCount:{} sensitivity:{} "
        "sensitivityRatio:{} filterToDenominator:{} senHitCount:{} senTotalCount:{}",
        task_id, m_params.sensitivityType, m_params.durationMs, m_params.durationMsMax,
        m_params.trackDetCount, m_params.sensitivity, m_params.sensitivityRatio, m_params.filterToDenominator,
        m_params.senHitCount, m_params.senTotalCount);
    return false;
}

// Set parameters - clear previous ones and set fully new ones
bool Sensitivity::SetParam(const std::string& /*channelId*/, const std::string& /*taskId*/,
                           std::vector<MsgDynamicKeyValue>& params) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    // Clear parameters first
    m_params = {};
    for (auto& param : params) {
        AnalysisKey(param, m_params);
    }
    LOG_INFO(
        "ModifyParam "
        "[{}] Set sensitivityType:{} durationMs:{}-{} trackDetCount:{} sensitivity:{} "
        "sensitivityRatio:{} filterToDenominator:{} senHitCount:{} senTotalCount:{}",
        task_id, m_params.sensitivityType, m_params.durationMs, m_params.durationMsMax,
        m_params.trackDetCount, m_params.sensitivity, m_params.sensitivityRatio, m_params.filterToDenominator,
        m_params.senHitCount, m_params.senTotalCount);
    return false;
}

// Set areas - clear previous ones and set fully new ones
bool Sensitivity::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                          std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    m_taskArea.taskId        = taskId;
    m_taskArea.areas         = areas;
    m_taskArea.shieldedAreas = shieldedAreas;
    for (auto& area : m_taskArea.areas) {
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& assArea : area.associatedAreas) {
            assArea.iderectionType = GetDirectionTypeFromMsg(assArea.params);
        }
    }

    m_mainAreas = areas;
    m_areas     = areas;
    for (auto& area : areas) {
        if (area.bHaveAssoArea) {
            auto assoAreaUnit = GetAssoAreas(area.associatedAreas);
            m_assoAreas.insert(m_assoAreas.end(), assoAreaUnit.begin(), assoAreaUnit.end());
        }
    }
    m_areas.insert(m_areas.end(), m_assoAreas.begin(), m_assoAreas.end());
    return true;
}

TaskBaseArea Sensitivity::GetArea() {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto area = m_taskArea;
    return area;
}

std::vector<MsgTaskArea> Sensitivity::GetAreas() {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto areas = m_areas;
    return areas;
}

// Track sensitivity methods — moved to SensitivityTrack.cc
// Area/AreaTarget/AreaThings sensitivity methods — moved to SensitivityArea.cc

void Sensitivity::HandFrame(AlgDataPtr algData) {
    need_alarm = false;
    if (!algData) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, invalid_frame_cnt);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    if (!((AlgDataType::ChannelDataDetect == algData->dataType) ||
          (AlgDataType::TaskDataTrack == algData->dataType) ||
          (AlgDataType::TaskDataClassify == algData->dataType) ||
          (AlgDataType::TaskDataRecognizer == algData->dataType) ||
          (AlgDataType::TaskDataFriendDistance == algData->dataType) ||
          (AlgDataType::TaskDataAiVideoQuality == algData->dataType) ||
          (AlgDataType::TaskDataClassifyMultPic == algData->dataType) ||
          (AlgDataType::TaskDataPersonFace == algData->dataType))) {
        invalid_frame_cnt += 1;
        if (0 == invalid_frame_cnt % 100) {
            LOG_WARN("{}[{}] Filter {} Frames dataType:{}", kTag, task_id, invalid_frame_cnt,
                     algData->dataType);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    stream_index = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetStreamIndex() : 0;
    frame_index  = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetFrameIndex() : 0;
    timestamp    = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetTimestamp() : 0;

    orig_fps = algData->chanDataOrig.fps;
    // Sensitivity of detection, tracking, classification
    if ((AlgDataType::ChannelDataDetect == algData->dataType) ||
        (AlgDataType::TaskDataTrack == algData->dataType) ||
        (AlgDataType::TaskDataClassify == algData->dataType) ||
        (AlgDataType::TaskDataFriendDistance == algData->dataType) ||
        (AlgDataType::TaskDataAiVideoQuality == algData->dataType) ||
        (AlgDataType::TaskDataClassifyMultPic == algData->dataType)) {
        DataDetTrackClassifyPtr input;
        if (AlgDataType::ChannelDataDetect == algData->dataType) {
            input = algData->chanDataDetect.detRet;
        } else if (AlgDataType::TaskDataTrack == algData->dataType) {
            input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
        } else if (AlgDataType::TaskDataFriendDistance == algData->dataType) {
            input = algData->GetTaskResult(AlgDataType::TaskDataFriendDistance);
        } else if (AlgDataType::TaskDataAiVideoQuality == algData->dataType) {
            input = algData->GetTaskResult(AlgDataType::TaskDataAiVideoQuality);
        } else if (AlgDataType::TaskDataClassifyMultPic == algData->dataType) {
            input = algData->taskDataClassifyMultPic.classifyRst;
        } else if (AlgDataType::TaskDataPersonFace == algData->dataType) {
            input = algData->GetTaskResult(AlgDataType::TaskDataTrack);
        } else {
            input = algData->GetTaskResult(AlgDataType::TaskDataClassify);
        }

        if (algData->bHaveTrack != m_haveTrack) {
            if (handle_frame_cnt > 0)
                LOG_WARN("{}[{}] WorkFlow Change To {}", kTag, task_id,
                         algData->bHaveTrack ? "Have Track" : "No Track");
            m_haveTrack = algData->bHaveTrack;
            // Track from none to some, need to clear m_mapTrackIdStatus
            // Track from some to none, need to clear m_mapAreaTargetStatus
        }
        if (algData->bHaveClassify != m_haveClassify) {
            if (handle_frame_cnt > 0)
                LOG_WARN("{}[{}] WorkFlow Change To {}", kTag, task_id,
                         algData->bHaveClassify ? "Have Classify" : "No Classify");
            m_haveClassify = algData->bHaveClassify;
        }

        if (m_haveTrack) {
            HandTrackData(algData, input);
        } else {
            HandAreaTargetData(algData, input);
        }
    }
    // Machine/object library sensitivity, filtering
    else if ((AlgDataType::TaskDataRecognizer == algData->dataType) ||
             (AlgDataType::TaskDataAiFilter == algData->dataType)) {
        HandAreaThingsData(algData);
    } else {
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }

    handle_frame_cnt += 1;
    if (0 == handle_frame_cnt % 100) {
        LOG_INFO("{}[{}] Handle {} Frames", kTag, task_id, handle_frame_cnt);
    }
    action_status = util::ErrorEnum::Success;
    // Push to subsequent (alarm) module when alarm is generated
    if ((algData->taskDataAlarm.alarmData) && (need_alarm)) {
        algData->taskDataAlarm.alarmData->multiAlarms += 1;
        distributor->DistributorData(algData);
    }
}

MsgOverviewMem Sensitivity::GetOverviewInfo(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                            int64_t streamIndex, int64_t from, int64_t to) {
    return m_overviewRecInst.GetOverviewInfo(streamIndex, from, to);
}
}  // namespace cosmo