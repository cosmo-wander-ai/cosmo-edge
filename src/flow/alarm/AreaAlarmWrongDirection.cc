// AreaAlarmWrongDirection.cc — Wrong-direction (retrograde) detection.
// Implementation partition of AreaAlarm (declared in flow/alarm/AreaAlarm.h).

#include "flow/alarm/AreaAlarm.h"
#include "flow/alarm/AreaAlarmInternalTypes.h"

static constexpr const char* kTag = "AreaAlarm ";

namespace cosmo {

// Get max/min values
void AreaAlarm::GetMaxMinValue(const std::deque<AiDetectRstEl>& history, const std::string& areaId,
                               float& max, float& min, int height) {
    if (height <= 0)
        return;
    max = -1.0f;
    min = -1.0f;
    for (auto& target : history) {
        if (target.bFilter) {
            continue;
        }
        if (has_area_) {
            // In shielded area
            if (!target.areaSign.shielded_areas.empty()) {
                continue;
            }
            // Not in area
            if (!areaIdInAreaUnits(target.areaSign.areas, areaId)) {
                continue;
            }
        }
        float value = static_cast<float>(target.point.y) / static_cast<float>(height);
        if (max < 0) {
            max = value;
        }
        if (min < 0) {
            min = value;
        }

        if (value > max) {
            max = value;
        }
        if (value < min) {
            min = value;
        }
    }

    return;
}

void AreaAlarm::HandAreaTargetWrongDirection(AlgDataPtr algData, PassFlowAreaTargets& areaTargets,
                                             PassFlowTrackIdData& areaData) {
    constexpr int kDefaultHeight = 1080;
    int height                   = kDefaultHeight;
    float max                    = -1.0f;
    float min                    = -1.0f;
    if (!algData || !algData->chanDataDec.frame) {
        return;
    }

    height = static_cast<int>(algData->chanDataDec.frame->GetHeight());
    if (height < 0)
        return;

    // Must be inside the area when alarming
    if (has_area_) {
        // In shielded area
        if (!areaData.target.areaSign.shielded_areas.empty()) {
            return;
        }
        // Not in area
        if (!areaIdInAreaUnits(areaData.target.areaSign.areas, areaTargets.area_id)) {
            return;
        }
    }

    GetMaxMinValue(areaData.history, areaTargets.area_id, max, min, height);
    float value = static_cast<float>(areaData.target.point.y) / static_cast<float>(height);
    float dist  = 0.0;
    if ((value < 0.0f) || (value > 1.0f))
        return;
    if (RetroDirect::RetroDirectSouth == params_.retro_direct) {
        if ((max < 0.0f) || (max > 1.0f))
            return;
        dist = max - value;
    } else {
        if ((min < 0.0f) || (min > 1.0f))
            return;
        dist = value - min;
    }

    if (dist > params_.retro_distance) {
        DataAlarmUnit alarmUnit;
        alarmUnit.flowActionId = action_info_.flowActionId;
        alarmUnit.reportType   = OnEventsReportType::Trigger;
        alarmUnit.areaId       = areaTargets.area_id;
        alarmUnit.areaName     = areaTargets.area_name;
        alarmUnit.trackId      = areaData.target.trackId;
        alarmUnit.strTrackId   = areaData.target.trackIdInfo;
        alarmUnit.box          = areaData.target.box;
        alarmUnit.boxs.push_back(alarmUnit.box);
        alarmUnit.retroDirect = params_.retro_direct;
        for (auto& hisEl : areaData.history) {
            if (hisEl.bFilter) {
                continue;
            }
            DataAlarmTargetConfidence targetConf;
            targetConf.box       = hisEl.box;
            targetConf.targetPos = hisEl.targetPos;
            alarmUnit.targetHistory.push_back(targetConf);
        }

        FillAlarmData(algData, alarmUnit);
    }
}

void AreaAlarm::HandAreaWrongDirection(AlgDataPtr algData) {
    for (auto& pair : pass_flow_areas_map_) {
        for (auto& targetPair : pair.second.target_map) {
            HandAreaTargetWrongDirection(algData, pair.second, targetPair.second);
        }
    }
}

// kDirection — wrong direction (retrograde)
void AreaAlarm::HandWrongDirection(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    // Retrograde — reuses the pass-flow / traffic-flow data structures
    HandPassFlowAddHistory(algData, input);
    HandAreaWrongDirection(algData);
    HandPassFlowOldHistory(false);
}

}  // namespace cosmo
