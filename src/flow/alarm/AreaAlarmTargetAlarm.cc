// AreaAlarmTargetAlarm.cc — Target alarm logic (intrusion / tripwire / distance detection).
// Implementation partition of AreaAlarm (declared in flow/alarm/AreaAlarm.h).

#include <algorithm>

#include "flow/alarm/AreaAlarm.h"
#include "flow/alarm/AreaAlarmInternalTypes.h"
#include "util/Log.h"

static constexpr const char* kTag = "AreaAlarm ";

namespace chrono = std::chrono;
namespace cosmo {

bool AreaAlarm::TargetInArea(const AiDetectRstEl& target, const std::string& areaId) {
    return std::any_of(target.areaSign.areas.begin(), target.areaSign.areas.end(),
                       [&areaId](const auto& area) { return area.area_id == areaId; });
}

// Target alarm condition judgement
DataAlarmUnit AreaAlarm::BuildAlarmUnit(const TrackIdData& idData, const MsgTaskArea& area) const {
    DataAlarmUnit unit;
    unit.flowActionId = action_info_.flowActionId;
    unit.reportType   = OnEventsReportType::Trigger;
    unit.areaId       = area.areaId;
    unit.areaName     = area.name;
    unit.trackId      = idData.track_id;
    unit.strTrackId   = idData.det_el.trackIdInfo;
    unit.box          = idData.det_el.box;
    unit.boxs.push_back(idData.det_el.box);
    unit.haveRelated = idData.det_el.relatedEl.bActive;
    unit.relatedBox  = idData.det_el.relatedEl.box;
    unit.feature     = idData.det_el.feature;

    for (const auto& subEl : idData.det_el.subs) {
        unit.boxs.push_back(subEl.box);
    }

    if (unit.haveRelated) {
        unit.confidence = idData.det_el.relatedEl.classifyRst;
        if (idData.det_el.subs.empty()) {
            unit.boxs.push_back(unit.relatedBox);
        }
    } else {
        unit.confidence = idData.det_el.classifyRst;
    }

    unit.targetHistory = idData.det_els;
    return unit;
}

// Target alarm condition judgement
void AreaAlarm::AreaTargetAlarmHandAlarm(AlgDataPtr algData, TrackIdData& idData, MsgTaskArea& area,
                                         TrackIdAreaData& areaData) {
    if (!areaData.has_into_area) {
        return;
    }

    auto& backEl = areaData.history.back();

    // Detection mode: alarm immediately when target is in area
    if (0 == params_.area_duration) {
        if (backEl.is_in_area) {
            auto alarmUnit = BuildAlarmUnit(idData, area);
            FillAlarmData(algData, alarmUnit);
        }
        return;
    }

    // Duration mode: check if target stayed long enough
    bool timeReached   = false;
    auto checkDuration = chrono::duration_cast<chrono::milliseconds>(backEl.data_time_point -
                                                                     areaData.history.front().data_time_point)
                             .count();
    while (checkDuration > static_cast<int64_t>(params_.area_duration)) {
        areaData.history.pop_front();
        timeReached   = true;
        checkDuration = chrono::duration_cast<chrono::milliseconds>(backEl.data_time_point -
                                                                    areaData.history.front().data_time_point)
                            .count();
    }

    if (!timeReached) {
        return;
    }

    // Check if all history entries are inside the area
    auto iterFind = std::find_if(areaData.history.begin(), areaData.history.end(),
                                 [](const TrackIdAreaDataUnit& u) { return !u.is_in_area; });
    if (iterFind != areaData.history.end()) {
        return;
    }

    // All entries in area — check break-area type to decide alarm
    if (BreakAreaType::kInto == break_area_type_ && areaData.has_out_area) {
        areaData.history.clear();
        auto alarmUnit = BuildAlarmUnit(idData, area);
        FillAlarmData(algData, alarmUnit);
    } else if (BreakAreaType::kIn == break_area_type_) {
        areaData.history.clear();
        auto alarmUnit = BuildAlarmUnit(idData, area);
        FillAlarmData(algData, alarmUnit);
    }
}

void AreaAlarm::AreaTargetAlarmHandArea(AlgDataPtr algData, TrackIdData& idData) {
    for (auto& area : task_area_.areas) {
        // Record area enter/exit status
        auto& areaData = idData.area_data[area.areaId];
        TrackIdAreaDataUnit areaTargetUnit;
        areaTargetUnit.data_time_point = idData.data_time_point;
        areaTargetUnit.frame_index     = frame_index_;
        if (!idData.target_in_shielded_area) {  // Not in shielded area
            areaTargetUnit.is_in_area = TargetInArea(idData.det_el, area.areaId);
        }

        if (areaTargetUnit.is_in_area) {
            areaData.has_into_area = true;
        } else {
            areaData.has_out_area = true;
        }

        areaData.history.push_back(areaTargetUnit);
        // Handle area alarm
        AreaTargetAlarmHandAlarm(algData, idData, area, areaData);
    }
}

bool AreaAlarm::CheckBreakAllArea(const std::vector<std::string>& areas,
                                  const std::vector<MsgTaskArea>& localAreas, std::string& outAreaId,
                                  std::string& outAreaName) {
    // Multi-line crossing: alarm based on line-crossing count
    size_t trippingWireType = params_.tripping_wire_type;
    if (areas.size() >= trippingWireType)
        return true;
    return false;

    for (auto& area : localAreas) {
        if (std::find(areas.begin(), areas.end(), area.areaId) != areas.end()) {
            outAreaId   = area.areaId;
            outAreaName = area.name;
            std::string areaId;
            std::string areaName;
            if (area.bHaveAssoArea) {
                return CheckBreakAllArea(areas, area.associatedAreas, areaId, areaName);
            } else {
                return true;
            }
        }
    }

    return false;
}

void AreaAlarm::AreaTargetAlarmHandLine(AlgDataPtr algData, TrackIdData& idData) {
    for (auto& line : idData.det_el.areaSign.lines) {
        if ((BreakAreaType::kLinePos == break_area_type_) && (TargetBreakLineType::kPos == line.status)) {
            // Must not repeatedly cross the same line
            if (std::find(idData.pos_break_line_areas.begin(), idData.pos_break_line_areas.end(),
                          line.area_id) != idData.pos_break_line_areas.end()) {
                continue;
            }
            idData.pos_break_line_areas.push_back(line.area_id);
            DataAlarmUnit alarmUnit;
            alarmUnit.flowActionId = action_info_.flowActionId;
            alarmUnit.reportType   = OnEventsReportType::Trigger;
            alarmUnit.areaId       = line.area_id;
            alarmUnit.areaName     = line.area_name;
            alarmUnit.trackId      = idData.track_id;
            alarmUnit.strTrackId   = idData.det_el.trackIdInfo;
            alarmUnit.box          = idData.det_el.box;
            alarmUnit.boxs.push_back(idData.det_el.box);
            alarmUnit.targetHistory = idData.det_els;
            if (CheckBreakAllArea(idData.pos_break_line_areas, task_area_.areas, alarmUnit.areaId,
                                  alarmUnit.areaName)) {
                FillAlarmData(algData, alarmUnit);
                idData.pos_break_line_areas.clear();
            }
        } else if ((BreakAreaType::kLineNeg == break_area_type_) &&
                   (TargetBreakLineType::kNeg == line.status)) {
            DataAlarmUnit alarmUnit;
            alarmUnit.flowActionId = action_info_.flowActionId;
            alarmUnit.reportType   = OnEventsReportType::Trigger;
            alarmUnit.areaId       = line.area_id;
            alarmUnit.areaName     = line.area_name;
            alarmUnit.trackId      = idData.track_id;
            alarmUnit.strTrackId   = idData.det_el.trackIdInfo;
            alarmUnit.box          = idData.det_el.box;
            alarmUnit.boxs.push_back(idData.det_el.box);
            alarmUnit.targetHistory = idData.det_els;
            idData.neg_break_line_areas.push_back(line.area_id);
            if (CheckBreakAllArea(idData.neg_break_line_areas, task_area_.areas, alarmUnit.areaId,
                                  alarmUnit.areaName)) {
                FillAlarmData(algData, alarmUnit);
                idData.neg_break_line_areas.clear();
            }
        }
    }
}

void AreaAlarm::TargetOldHistory() {
    for (auto it = track_id_status_map_.begin(); it != track_id_status_map_.end();) {
        if (!it->second.detected) {
            it = track_id_status_map_.erase(it);
        } else {
            it->second.detected = false;
            ++it;
        }
    }
}

// kTargetAlarm — intrusion / tripwire / garbage pile, etc.
void AreaAlarm::HandAreaTargetAlarm(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    // breakAreaType      Area entry mode
    // areaDuration        Duration threshold
    constexpr size_t kMaxTargetHistory = 32;
    for (auto& target : input->targets) {
        // Record target
        if (target.bFilter) {
            continue;
        }
        auto& idData = track_id_status_map_[target.trackId];
        if (target.trackId != idData.track_id) {
            idData.det_els.reserve(kMaxTargetHistory);
        }
        idData.track_id   = target.trackId;
        idData.detected   = true;
        target.frameIndex = frame_index_;
        idData.det_el     = target;
        DataAlarmTargetConfidence targetHisEl;
        targetHisEl.box       = target.box;
        targetHisEl.targetPos = target.targetPos;
        if (idData.det_els.size() >= kMaxTargetHistory) {
            idData.det_els.erase(idData.det_els.begin());
        }
        idData.det_els.push_back(targetHisEl);
        idData.data_time_point  = algData->firstTimePoint;
        bool targetInShiledArea = !idData.det_el.areaSign.shielded_areas.empty();
        if (targetInShiledArea != idData.target_in_shielded_area) {
            LOG_INFO("{}[{}] {}/{} At Frame {}. trackId:{} {} ShiledArea", kTag, task_id,
                     action_info_.actionName, action_info_.flowActionId, frame_index_, idData.track_id,
                     targetInShiledArea ? "INTO" : "OUT");
            idData.target_in_shielded_area = targetInShiledArea;
        }
        if (break_area_type_ < BreakAreaType::kAreaMax) {
            // Handle area alarm — intrusion
            AreaTargetAlarmHandArea(algData, idData);
        } else if (break_area_type_ < BreakAreaType::kLineMax) {
            // Handle line alarm — tripwire
            AreaTargetAlarmHandLine(algData, idData);
        }
    }
    TargetOldHistory();
}

void AreaAlarm::HandFriendTargetAlarm(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    // Cylinder-flame distance detection — no tracking, alarm on area entry
    for (auto& target : input->groupTargets) {
        if (!target.bLogicResult) {
            continue;
        }
        if (!target.genTarget.areaSign.shielded_areas.empty()) {
            continue;
        }
        for (auto area : target.genTarget.areaSign.areas) {
            DataAlarmUnit alarmUnit;
            alarmUnit.flowActionId = GetActionId();
            alarmUnit.areaId       = area.area_id;
            alarmUnit.areaName     = area.area_name;
            alarmUnit.box          = target.genTarget.box;
            for (auto& friendTarget : target.srcTargets) {
                alarmUnit.friends.push_back(friendTarget.box);
                alarmUnit.boxs.push_back(friendTarget.box);
            }

            FillAlarmData(algData, alarmUnit);
        }
    }
}

}  // namespace cosmo
