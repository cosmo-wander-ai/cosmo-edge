// AreaAlarmTargetLimit.cc — Target-limit alarm logic (absence / crowd detection).
// Implementation partition of AreaAlarm (declared in flow/alarm/AreaAlarm.h).

#include "flow/alarm/AreaAlarm.h"
#include "flow/alarm/AreaAlarmInternalTypes.h"
#include "util/Log.h"

static constexpr const char* kTag = "AreaAlarm ";

namespace chrono = std::chrono;
namespace cosmo {

void AreaAlarm::AddAreaTargetHistory(DataDetTrackClassifyPtr input,
                                     chrono::steady_clock::time_point& /*dataTimePoint*/) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto areaDuration = params_.area_duration;
    for (auto& pair : area_target_status_map_) {
        AreaTarget areaTarget;
        areaTarget.stream_index = input->streamIndex;
        areaTarget.frame_index  = input->frameIndex;
        areaTarget.timestamp    = input->timestamp;
        for (auto& target : input->targets) {
            // Targets in shielded area are excluded
            if (!target.areaSign.shielded_areas.empty()) {
                continue;
            }
            for (const auto& targetArea : target.areaSign.areas) {
                if (targetArea.area_id == pair.first) {
                    AiDetectRstEl detTarget = target;
                    areaTarget.targets.push_back(target);
                }
            }
        }
        pair.second.history.push_back(areaTarget);
        while ((pair.second.history.size() >= 2) &&
               ((pair.second.history.back().timestamp - pair.second.history[0].timestamp) >
                static_cast<int64_t>(areaDuration))) {
            pair.second.history.pop_front();
            pair.second.history_full = true;
        }
        if (0 == params_.area_duration) {
            pair.second.history_full = true;
        }
    }
}

void AreaAlarm::MultTargetAddTarget(std::vector<MultTargetAreaLimitParam>& targetsInfo,
                                    const AiDetectRstEl& target) {
    auto it = std::find_if(
        targetsInfo.begin(), targetsInfo.end(),
        [&](const MultTargetAreaLimitParam& dataUnit) { return target.confidence.label == dataUnit.label; });
    if (it != targetsInfo.end()) {
        it->area_limit_target_count += 1;
        return;
    }

    MultTargetAreaLimitParam dataUnit;
    dataUnit.area_limit_target_count = 1;
    dataUnit.label                   = target.confidence.label;
    targetsInfo.push_back(dataUnit);
    return;
}

bool AreaAlarm::MultTargetLogicResult(std::vector<MultTargetAreaLimitParam>& targetsInfo) {
    // No conditions — return false
    if (params_.mult_target_limit_param.empty()) {
        return false;
    }

    for (auto& param : params_.mult_target_limit_param) {
        int areaTargetCount = 0;
        auto it             = std::find_if(
                        targetsInfo.begin(), targetsInfo.end(),
                        [&](const MultTargetAreaLimitParam& dataUnit) { return param.label == dataUnit.label; });
        if (it != targetsInfo.end()) {
            areaTargetCount = it->area_limit_target_count;
        }
        bool ret = AlgCompareTypeResult(param.area_limit_target_type, areaTargetCount,
                                        param.area_limit_target_count);

        if (!ret) {
            return ret;
        }
    }

    return true;
}

bool AreaAlarm::GetAreaTargetLimitResult(const AreaTarget& areaTarget) {
    int areaTargetCount = 0;
    std::vector<MultTargetAreaLimitParam> targetsInfo;
    for (auto& target : areaTarget.targets) {
        if ((target.bFilter) && (target.filterType != AIFilterType::NoRelatedTarget)) {
            continue;
        }
        areaTargetCount += 1;
        if (AreaAlarmType::kTargetLimitMultTargets == type_) {
            MultTargetAddTarget(targetsInfo, target);
        }
    }
    bool ret = false;
    if (AreaAlarmType::kTargetLimitMultTargets == type_) {
        ret = MultTargetLogicResult(targetsInfo);
    } else {
        ret = AlgCompareTypeResult(params_.area_limit_target_type, areaTargetCount,
                                   params_.area_limit_target_count);
    }

    return ret;
}

// Handle area target limit — e.g. max persons for absence, min persons for crowd
void AreaAlarm::HandAreaTargetLimit(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    // areaLimitTargetType   Limit comparison mode
    // areaLimitTargetCount  Limit target count
    // areaDuration          Duration threshold
    AddAreaTargetHistory(input, algData->firstTimePoint);

    std::shared_lock<std::shared_mutex> lock(mtx);

    auto areaDuration = params_.area_duration;
    for (auto& pair : area_target_status_map_) {
        if ((!pair.second.history_full) || (pair.second.history.empty())) {
            continue;
        }

        if (0 == areaDuration) {
            if (GetAreaTargetLimitResult(pair.second.history.back())) {
                DataAlarmUnit alarmUnit;
                alarmUnit.flowActionId = action_info_.flowActionId;
                alarmUnit.areaId       = pair.second.area_id;
                alarmUnit.areaName     = pair.second.area_name;
                alarmUnit.reportType   = OnEventsReportType::Trigger;
                for (auto& target : pair.second.history.back().targets) {
                    if ((target.bFilter) && (target.filterType != AIFilterType::NoRelatedTarget)) {
                        continue;
                    }
                    alarmUnit.boxs.push_back(target.box);
                }
                FillAlarmData(algData, alarmUnit);
            }
        } else {
            bool needAlarm = true;
            int totalCount = 0;
            int hitCount   = 0;
            bool bHaveSen  = ((params_.area_sensitivity > 0) && (params_.area_sensitivity <= 10));
            for (auto& history : pair.second.history) {
                totalCount += 1;
                if (!GetAreaTargetLimitResult(history)) {
                    needAlarm = false;
                    if (!bHaveSen) {
                        break;
                    }
                } else {
                    hitCount += 1;
                }
            }

            if (bHaveSen) {
                float ratio = static_cast<float>(hitCount) / totalCount;
                if (ratio >= params_.area_sensitivity_ratio) {
                    needAlarm = true;
                    LOG_INFO(
                        "{}[{}] {}/{} Target-limit within duration [RatioTrue] Area:{}  At Frame:{} "
                        "hitCount:{} "
                        "totalCount:{}",
                        kTag, task_id, action_info_.actionName, action_info_.flowActionId,
                        pair.second.area_name, frame_index_, hitCount, totalCount);
                } else {
                    needAlarm = false;
                }
            }

            if (needAlarm) {
                DataAlarmUnit alarmUnit;
                alarmUnit.flowActionId = action_info_.flowActionId;
                alarmUnit.reportType   = OnEventsReportType::Trigger;
                alarmUnit.areaId       = pair.second.area_id;
                alarmUnit.areaName     = pair.second.area_name;
                for (auto& target : pair.second.history.back().targets) {
                    if ((target.bFilter) && (target.filterType != AIFilterType::NoRelatedTarget)) {
                        continue;
                    }
                    alarmUnit.boxs.push_back(target.box);
                    if (target.bestEl.bActive) {
                        alarmUnit.bestInfos.push_back(target.bestEl);
                    }
                }
                LOG_INFO("bestInfos:{} boxSize:{}", alarmUnit.bestInfos.size(), alarmUnit.boxs.size());
                FillAlarmData(algData, alarmUnit);
            }
        }
    }
}

}  // namespace cosmo
