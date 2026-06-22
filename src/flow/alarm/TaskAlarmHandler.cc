// TaskAlarmHandler.cc — Alarm handling for TaskAlarm.
// Split from TaskAlarm.cc to reduce file size (DEBT-007).

#include <algorithm>
#include <sstream>

#include "flow/alarm/TaskAlarm.h"
#include "flow/alarm/TaskAlarmInternalTypes.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/common/LlmYesNoJudge.h"
#include "media/VideoFrame.h"
#include "service/camera/ICameraChannelQuery.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmRecordService.h"
#include "service/event/IEventNotifier.h"
#include "service/path/IFileService.h"
#include "util/Log.h"
#include "util/NToL.h"
#include "util/PathUtil.h"
#include "util/Rect.h"
#include "util/dto/ClientMsgEvent.h"

static constexpr const char* kTag = "TaskAlarm ";
namespace cosmo {

// Set areas — clear previous areas and apply full replacement
bool TaskAlarm::SetArea(const std::string& /*channelId*/, const std::string& taskId,
                        std::vector<MsgTaskArea>& areas, std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx);
    m_areaHaveAsso           = false;
    m_areaAssoIsArea         = false;
    m_taskArea.taskId        = taskId;
    m_taskArea.areas         = areas;
    m_taskArea.shieldedAreas = shieldedAreas;
    for (auto& area : m_taskArea.areas) {
        area.iderectionType = GetDirectionTypeFromMsg(area.params);
        for (auto& assArea : area.associatedAreas) {
            assArea.iderectionType = GetDirectionTypeFromMsg(assArea.params);
            if ((!assArea.points.empty()) || (!assArea.linePoints.empty()))  // Area must contain points
            {
                m_areaHaveAsso = true;
            }

            // Associated area is a region; primary area is also a region
            if (!assArea.points.empty() && !area.points.empty()) {
                m_areaAssoIsArea = true;
            }
        }
    }
    return true;
}

void TaskAlarm::TrackAdd(AlgDataPtr algData) {
    for (const auto& alarmUnit : algData->taskDataAlarm.alarmData->alarms) {
        auto& idData   = m_mapAlarmIdStatus[alarmUnit.trackId];
        idData.trackId = alarmUnit.trackId;
        idData.sign    = true;
    }
}

bool TaskAlarm::TrackIsDispare(AlgDataPtr algData, int trackId) {
    if (trackId <= 0) {
        return false;  // May have no tracking
    }

    auto taskResult = algData->GetTaskResult(AlgDataType::TaskDataTrack);
    if (!taskResult) {
        return false;  // May have no tracking
    }

    const auto& targets = taskResult->targets;
    return std::none_of(targets.begin(), targets.end(),
                        [trackId](const auto& target) { return trackId == target.trackId; });
}

void TaskAlarm::TrackOld(AlgDataPtr algData) {
    for (auto it = m_mapAlarmIdStatus.begin(); it != m_mapAlarmIdStatus.end();) {
        if (TrackIsDispare(algData, it->second.trackId)) {
            it = m_mapAlarmIdStatus.erase(it);
        } else {
            ++it;
        }
    }
}

std::pair<bool, DataAlarmUnit> TaskAlarm::findAssoAreaAlarm(const std::string& alarmFlowActionId,
                                                            const std::deque<DataAlarmUnit>& alarms,
                                                            const std::vector<MsgTaskArea>& assoAreas,
                                                            unsigned int multiAlarms) {
    auto it = std::find_if(alarms.begin(), alarms.end(), [&](const auto& alarm) {
        if (!(((multiAlarms > 1) && (alarmFlowActionId != alarm.flowActionId)) || (1 == multiAlarms))) {
            return false;
        }
        return std::any_of(assoAreas.begin(), assoAreas.end(),
                           [&alarm](const auto& assoArea) { return alarm.areaId == assoArea.areaId; });
    });
    if (it != alarms.end()) {
        return std::make_pair(true, *it);
    }

    return std::make_pair(false, DataAlarmUnit{});
}

DataAlarmUnit TaskAlarm::AlarmDataCombineAlarm(DataAlarmUnit alarm, DataAlarmUnit assoAlarm) {
    DataAlarmUnit unit;
    // Find the alarm with a trackId as the primary alarm
    if (alarm.trackId < 0) {
        unit = assoAlarm;
        // Use primary alarm's areaId for overlay region lookup
        unit.areaId   = alarm.areaId;
        unit.areaName = alarm.areaName;
        if (!alarm.boxs.empty())
            unit.boxs.insert(unit.boxs.end(), alarm.boxs.begin(), alarm.boxs.end());
        if (alarm.areaId != assoAlarm.areaId) {
            // Primary alarm's associated area for overlay region lookup
            unit.assoAreaId   = assoAlarm.areaId;
            unit.assoAreaName = assoAlarm.areaName;
            LOG_INFO("{}[{}] CombineAlarm:{} And {} Boxs {}+{}={} alarmTrack:{} assoTrack:{}", kTag, task_id,
                     alarm.areaId, assoAlarm.areaId, alarm.boxs.size(), assoAlarm.boxs.size(),
                     unit.boxs.size(), alarm.trackId, assoAlarm.trackId);
        }

    } else {
        unit = alarm;
        if (!assoAlarm.boxs.empty())
            unit.boxs.insert(unit.boxs.end(), assoAlarm.boxs.begin(), assoAlarm.boxs.end());
        if (alarm.areaId != assoAlarm.areaId) {
            unit.assoAreaId   = assoAlarm.areaId;
            unit.assoAreaName = assoAlarm.areaName;
            LOG_INFO("{}[{}] CombineAlarm:{} And {} Boxs {}+{}={} alarmTrack:{} assoTrack:{}", kTag, task_id,
                     alarm.areaId, assoAlarm.areaId, alarm.boxs.size(), assoAlarm.boxs.size(),
                     unit.boxs.size(), alarm.trackId, assoAlarm.trackId);
        }
    }

    if ((OnEventsReportType::Trigger == alarm.reportType) ||
        (OnEventsReportType::Trigger == assoAlarm.reportType)) {
        unit.reportType = OnEventsReportType::Trigger;
    }

    return unit;
}

std::deque<DataAlarmUnit> TaskAlarm::AlarmDataCombineWithAsso(AlgDataPtr algData) {
    std::deque<DataAlarmUnit> combAlarms;
    auto lacalAreas = GetAreas();
    auto alarms     = algData->taskDataAlarm.alarmData->alarms;
    for (auto& localArea : lacalAreas) {
        for (auto& alarm : alarms) {
            if (alarm.areaId == localArea.areaId) {
                // Find the alarm from the associated area
                auto assoAlarm = findAssoAreaAlarm(alarm.flowActionId, alarms, localArea.associatedAreas,
                                                   algData->taskDataAlarm.alarmData->multiAlarms);
                if (assoAlarm.first) {
                    // Merge alarms
                    auto alarmUnit = AlarmDataCombineAlarm(alarm, assoAlarm.second);
                    combAlarms.push_back(alarmUnit);
                }
            }
        }
    }

    return combAlarms;
}

std::deque<DataAlarmUnit> TaskAlarm::AlarmDataCombineNoAsso(AlgDataPtr algData) {
    std::deque<DataAlarmUnit> combAlarms;
    auto alarms = algData->taskDataAlarm.alarmData->alarms;
    if (alarms.size() < 2) {
        return alarms;
    }
    for (size_t i = 0; i < alarms.size(); i++) {
        for (size_t j = i + 1; j < alarms.size(); j++) {
            // Two alarms from different flows
            if ((alarms[i].areaId == alarms[j].areaId) &&
                (alarms[i].flowActionId != alarms[j].flowActionId)) {
                // Merge alarms
                auto alarmUnit = AlarmDataCombineAlarm(alarms[i], alarms[j]);
                combAlarms.push_back(alarmUnit);
            }
        }
    }

    return combAlarms;
}

void TaskAlarm::AlarmDataCombine(AlgDataPtr algData) {
    if (!((m_areaAssoIsArea) || (algData->taskDataAlarm.alarmData->multiAlarms > 1))) {
        return;
    }

    std::deque<DataAlarmUnit> combAlarms;
    if (m_areaHaveAsso) {
        combAlarms = AlarmDataCombineWithAsso(algData);
    } else {
        combAlarms = AlarmDataCombineNoAsso(algData);
    }

    LOG_INFO("{}[{}] multiAlarms:{} m_areaHaveAsso:{} CombineAlarm AlarmCount From {} To {}", kTag, task_id,
             algData->taskDataAlarm.alarmData->multiAlarms, m_areaHaveAsso,
             algData->taskDataAlarm.alarmData->alarms.size(), combAlarms.size());

    algData->taskDataAlarm.alarmData->alarms = combAlarms;
}

void TaskAlarm::HandFrame(AlgDataPtr algData) {
    if (!algData) {
        m_filterFrames += 1;
        if (0 == m_filterFrames % 100) {
            LOG_WARN("{}[{}] Filter {} Frames", kTag, task_id, m_filterFrames);
        }
        action_status = util::ErrorEnum::FlowDataInvalid;
        return;
    }
    if (!algData->taskDataAlarm.alarmData) {
        return;
    }

    m_width  = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetWidth() : m_width;
    m_height = algData->chanDataDec.frame ? algData->chanDataDec.frame->GetHeight() : m_height;

    AlarmDataCombine(algData);

    TrackAdd(algData);
    FillAlarmData(algData);

    TrackOld(algData);

    m_handleFrames += 1;

    if (0 == m_handleFrames % 100) {
        LOG_INFO("{}[{}] Handle {} Frames", kTag, task_id, m_handleFrames);
    }
}

MsgTaskArea TaskAlarm::GetArea(const std::string& areaId) {
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto it = std::find_if(m_taskArea.areas.begin(), m_taskArea.areas.end(),
                           [&areaId](const auto& area) { return areaId == area.areaId; });
    if (it != m_taskArea.areas.end()) {
        return *it;
    }

    return {};
}

std::vector<MsgTaskArea> TaskAlarm::GetAreas() {
    std::shared_lock<std::shared_mutex> lock(mtx);

    return m_taskArea.areas;
}

util::Point TaskAlarm::GetPos(const util::Box& box, TargetPosition targetPos) {
    util::Point point;
    point.x = box.x + box.width / 2;
    if (targetPos == TargetPosition::kBottom) {
        point.y = box.y + box.height;
    } else if (targetPos == TargetPosition::kCenter) {
        point.y = box.y + box.height / 2;
    } else {
        point.y = box.y;
    }

    return point;
}

MsgOverviewMem TaskAlarm::GetOverviewInfo(const std::string& /*channelId*/, const std::string& /*taskId*/,
                                          int64_t streamIndex, int64_t from, int64_t to) {
    return m_overviewRecInst.GetOverviewInfo(streamIndex, from, to);
}

std::string TaskAlarm::GetChannelName() const {
    return service::ServiceRegistry::Instance().Get<service::ICameraChannelQuery>().GetChannelName(
        GetChannel());
}

std::string TaskAlarm::GetAlgCategory() const {
    if (action_alg) {
        return action_alg->category;
    }
    return "";
}

std::string TaskAlarm::GetAlgId() const {
    if (action_alg) {
        return action_alg->algorithmCode;
    }
    return "";
}

std::string TaskAlarm::GetAlgName() const {
    if (action_alg) {
        return action_alg->algorithmName;
    }
    return "";
}

}  // namespace cosmo
