// SensitivityTrack.cc — Track-based sensitivity calculation for Sensitivity.
// Split from Sensitivity.cc to reduce file size (DEBT-007).

#include "flow/common/AlgDataRecord.h"
#include "flow/sensitivity/SensitivityTypes.h"
#include "util/Keys.h"
#include "util/Log.h"

static constexpr const char* kTag = "Sensitivity ";
namespace cosmo {

void Sensitivity::AddHistory(DataDetTrackClassifyPtr input,
                             const std::chrono::steady_clock::time_point& dataTimePoint) {
    for (auto& target : input->targets) {
        auto& idData = m_mapTrackIdStatus[static_cast<unsigned int>(target.trackId)];
        if (idData.trackIdUuid.empty())  // Appears for the first time
        {
            idData.trackIdUuid = target.trackIdInfo;
            LOG_AITARGET("{}[{}] trackId {} Appear", kTag, task_id, target.trackId);
        }
        idData.trackId  = static_cast<unsigned int>(target.trackId);
        idData.detected = true;
        idData.target   = target;
        TrackIdData::TrackIdDataEl trackData;

        trackData.timePoint                      = dataTimePoint;
        trackData.bLogicResult                   = target.bLogicResult;
        trackData.bFilter                        = target.bFilter;
        trackData.filterDesc                     = target.filterDesc;
        trackData.targetCondidenceInfo.box       = target.box;
        trackData.targetCondidenceInfo.targetPos = target.targetPos;

        // Area & Shild Area
        trackData.bHaveArea         = input->bHaveArea;
        trackData.bHaveShieldedArea = input->bHaveShieldedArea;
        for (auto& shiledArea : target.areaSign.shielded_areas) {
            trackData.shildAreas.push_back({shiledArea.area_id, shiledArea.area_name});
        }
        for (auto& targetAreas : target.areaSign.areas) {
            trackData.areas.push_back({targetAreas.area_id, targetAreas.area_name});
        }

        idData.history.push_back(trackData);
    }
}

void Sensitivity::OldHistory() {
    for (auto it = m_mapTrackIdStatus.begin(); it != m_mapTrackIdStatus.end();) {
        if (SensitivityType::kFixCount == m_params.sensitivityType) {
            while ((it->second.history.size() > m_params.senTotalCount)) {
                it->second.history.pop_front();
            }
            it->second.historyFull = !it->second.history.empty();
        } else {
            // At least two nodes and (first node time exceeds upper limit time or second node time exceeds
            // actual time)
            while (
                (it->second.history.size() >= 2) &&
                ((std::chrono::duration_cast<std::chrono::milliseconds>(it->second.history.back().timePoint -
                                                                        it->second.history[1].timePoint)
                      .count() > m_params.durationMs)
                 //	&&
                 //(std::chrono::duration_cast<std::chrono::milliseconds>(it->second.history.back().timePoint
                 //- it->second.history.front().timePoint).count() > m_params.durationMs)
                 )) {
                it->second.history.pop_front();
                it->second.historyFull = true;
            }
        }

        ++it;
    }
}

void Sensitivity::CalcSensitity(AlgDataPtr algData) {
    bool filterToDenominator = m_params.filterToDenominator;
    auto paramRatio          = m_params.sensitivityRatio;
    for (auto it = m_mapTrackIdStatus.begin(); it != m_mapTrackIdStatus.end(); ++it) {
        TrackIdData& trackIdData = it->second;
        if (!trackIdData.historyFull) {  // Queue not full, cannot calculate sensitivity
            continue;
        }
        size_t detCount    = 0;
        size_t totalCount  = 0;
        size_t filterCount = 0;
        auto duration      = std::chrono::duration_cast<std::chrono::milliseconds>(
                            it->second.history.back().timePoint - it->second.history.front().timePoint)
                            .count();
        for (auto& el : trackIdData.history) {
            if (el.bLogicResult) {
                detCount += 1;
            }

            if (el.bFilter) {
                filterCount += 1;
            }
        }
        if (filterToDenominator) {
            totalCount = trackIdData.history.size();
        } else {
            totalCount = trackIdData.history.size() - filterCount;
        }

        if (SensitivityType::kFixCount == m_params.sensitivityType) {
            totalCount = m_params.senTotalCount;
        }

        // m_params.sensitivity, duration);
        if (totalCount > 0) {
            float ratio = static_cast<float>(detCount) / static_cast<float>(totalCount);
            if (ratio >= paramRatio) {
                LOG_INFO(
                    "{}[{}] trackId:{} [NEED ALARM] Detect:{} Total:{} queSize:{} At Frame:{} "
                    "sensitivity:{} ratio:{} paramRatio:{} detDuration:{}",
                    kTag, task_id, trackIdData.trackId, detCount, totalCount, trackIdData.history.size(),
                    frame_index, m_params.sensitivity, ratio, paramRatio, duration);

                DataAlarmUnit alarmUnit;
                alarmUnit.flowActionId = action_node.flowActionId;
                alarmUnit.reportType   = OnEventsReportType::Trigger;
                FillAlarmDataTrackId(alarmUnit, trackIdData);
                FillAlarmData(algData, alarmUnit);
                trackIdData.history.clear();
                trackIdData.historyFull = false;
            }
        }
    }
}

void Sensitivity::AddGroupHistory(DataDetTrackClassifyPtr input,
                                  const std::chrono::steady_clock::time_point& dataTimePoint) {
    for (auto& target : input->groupTargets) {
        auto& idData = m_mapTrackIdStatus[static_cast<unsigned int>(target.groupId)];
        if (idData.trackIdUuid.empty())  // Appears for the first time
        {
            idData.trackIdUuid = target.groupIdInfo;
            LOG_AITARGET("{}[{}] trackId {} Appear", kTag, task_id, target.groupId);
        }
        idData.trackId  = static_cast<unsigned int>(target.groupId);
        idData.detected = true;
        TrackIdData::TrackIdDataEl trackData;

        trackData.timePoint                = dataTimePoint;
        trackData.bLogicResult             = target.bLogicResult;
        trackData.bFilter                  = target.genTarget.bFilter;
        trackData.filterDesc               = target.genTarget.filterDesc;
        trackData.targetCondidenceInfo.box = target.genTarget.box;
        for (auto& srcTarget : target.srcTargets) {
            trackData.targetCondidenceInfo.friends.push_back(srcTarget.box);
        }
        trackData.targetCondidenceInfo.targetPos = target.genTarget.targetPos;

        // Area & Shild Area
        trackData.bHaveArea         = input->bHaveArea;
        trackData.bHaveShieldedArea = input->bHaveShieldedArea;
        for (auto& shiledArea : target.genTarget.areaSign.shielded_areas) {
            trackData.shildAreas.push_back({shiledArea.area_id, shiledArea.area_name});
        }
        for (auto& targetAreas : target.genTarget.areaSign.areas) {
            trackData.areas.push_back({targetAreas.area_id, targetAreas.area_name});
        }

        idData.history.push_back(trackData);
    }
}

// TargetSensitivityDetCount defined in SensitivityTypes.h

void Sensitivity::CalcSensitityWithArea(AlgDataPtr algData) {
    MsgRecSensitity recSensitity;
    bool haveRecRst          = false;
    recSensitity.index       = static_cast<int64_t>(frame_index);
    recSensitity.streamIndex = static_cast<int64_t>(stream_index);
    recSensitity.timestamp   = static_cast<int64_t>(timestamp);
    bool filterToDenominator = m_params.filterToDenominator;
    auto paramRatio          = m_params.sensitivityRatio;
    for (auto it = m_mapTrackIdStatus.begin(); it != m_mapTrackIdStatus.end(); ++it) {
        TrackIdData& trackIdData = it->second;
        if (trackIdData.history.empty()) {
            continue;
        }
        // if (!trackIdData.historyFull) // Queue not full, cannot calculate sensitivity
        // {
        // }

        if (!((m_haveClassify &&
               trackIdData.history.back().bLogicResult)  // Has classification, last frame must be true
              || ((!m_haveClassify) &&
                  trackIdData.history.back().bHaveArea)  // No classification, last frame must have area
              )) {  // Above two conditions required to calculate sensitivity
            continue;
        }
        std::string defaultArea = std::string(key::DEFAULT_AREA);  // No area state
        size_t totalCount       = 0;
        size_t filterCount      = 0;
        size_t detCount         = 0;
        auto duration           = std::chrono::duration_cast<std::chrono::milliseconds>(
                            trackIdData.history.back().timePoint - trackIdData.history.front().timePoint)
                            .count();
        std::map<std::string, TargetSensitivityDetCount> mapAreaDetCounts;
        for (auto& el : trackIdData.history) {
            if (el.bHaveArea) {
                for (auto area : el.areas) {
                    auto& areaDetCount    = mapAreaDetCounts[area.id];
                    areaDetCount.areaName = area.name;
                    areaDetCount.inArea   = true;
                    areaDetCount.totalCount +=
                        1;  //  totalCount == trackIdData.history.size() means always in area
                    areaDetCount.rsts.push_back(false);
                }
            } else {
                auto& areaDetCount  = mapAreaDetCounts[defaultArea];
                areaDetCount.inArea = true;
                areaDetCount.totalCount +=
                    1;  //  totalCount == trackIdData.history.size() means always in area
                areaDetCount.rsts.push_back(false);
            }

            if (el.bFilter) {
                filterCount += 1;
                for (auto area : el.areas) {
                    auto& areaDetCount    = mapAreaDetCounts[area.id];
                    areaDetCount.areaName = area.name;
                    if (areaDetCount.rsts.empty()) {
                        areaDetCount.rsts.push_back(false);
                    } else {
                        areaDetCount.rsts.back() = false;
                    }
                }
                continue;
            }

            // When there is classification, bLogicResult must be true
            // When there is no classification, detected is true
            if (el.bLogicResult || (!m_haveClassify)) {
                if (el.bHaveArea) {
                    for (auto area : el.areas) {
                        auto& areaDetCount    = mapAreaDetCounts[area.id];
                        areaDetCount.areaName = area.name;
                        areaDetCount.detectCount += 1;
                        if (areaDetCount.rsts.empty()) {
                            areaDetCount.rsts.push_back(false);
                        } else {
                            areaDetCount.rsts.back() = true;
                        }
                    }
                } else {
                    auto& areaDetCount = mapAreaDetCounts[defaultArea];
                    areaDetCount.detectCount += 1;
                    if (areaDetCount.rsts.empty()) {
                        areaDetCount.rsts.push_back(false);
                    } else {
                        areaDetCount.rsts.back() = true;
                    }
                }

                if (!el.shildAreas.empty()) {
                    LOG_AITARGET("{}[{}] trackId:{} Into ShiledArea", kTag, task_id, trackIdData.trackId);
                    for (auto itArea = mapAreaDetCounts.begin(); itArea != mapAreaDetCounts.end(); ++itArea) {
                        itArea->second.detectCount = 0;
                        itArea->second.totalCount  = 0;
                        itArea->second.rsts.clear();
                    }
                }
                for (auto itArea = mapAreaDetCounts.begin(); itArea != mapAreaDetCounts.end(); ++itArea) {
                    if (!itArea->second.inArea) {  // Exited area
                        itArea->second.detectCount = 0;
                        itArea->second.totalCount  = 0;
                        itArea->second.rsts.clear();
                    }
                    itArea->second.inArea = false;
                }
                detCount += 1;
            }
        }

        if (trackIdData.historyFull) {
            totalCount = trackIdData.history.size();
        } else {
            if (m_params.trackDetCount > 0) {
                totalCount = static_cast<size_t>(m_params.trackDetCount);
            } else if (orig_fps > 0) {
                totalCount =
                    static_cast<size_t>(orig_fps * static_cast<float>(m_params.durationMs) / 1000.0f) + 1;
            } else {  // No frame rate
                LOG_INFO("{}[{}] trackDetCount:{} orig_fps:{}", kTag, task_id, m_params.trackDetCount,
                         orig_fps);
                continue;
            }
        }

        if (!filterToDenominator) {
            totalCount = trackIdData.history.size() - filterCount;
        }

        if (SensitivityType::kFixCount == m_params.sensitivityType) {
            totalCount = m_params.senTotalCount;
        }

        if (totalCount > 0) {
            for (auto itArea = mapAreaDetCounts.begin(); itArea != mapAreaDetCounts.end(); ++itArea) {
                haveRecRst = true;

                float ratio = static_cast<float>(itArea->second.detectCount) / static_cast<float>(totalCount);
                itArea->second.demCount = totalCount;
                RecTrackTargetData(recSensitity, itArea->first, static_cast<int>(trackIdData.trackId),
                                   paramRatio, itArea->second);
                if (ratio >= paramRatio) {
                    std::string area;
                    if (itArea->first != defaultArea) {
                        area = itArea->first;
                    }
                    LOG_INFO(
                        "{}[{}] trackId:{} [NEED ALARM] In Area:{}/{} Detect:{} Total:{} queSize:{} At "
                        "Frame:{} sensitivity:{} ratio:{} paramRatio:{} detDuration:{}",
                        kTag, task_id, trackIdData.trackId, area, itArea->second.areaName,
                        itArea->second.detectCount, totalCount, trackIdData.history.size(), frame_index,
                        m_params.sensitivity, ratio, paramRatio, duration);

                    DataAlarmUnit alarmUnit;
                    alarmUnit.flowActionId = action_node.flowActionId;
                    alarmUnit.reportType   = OnEventsReportType::Trigger;
                    alarmUnit.areaId       = area;
                    alarmUnit.areaName     = itArea->second.areaName;
                    FillAlarmDataTrackId(alarmUnit, trackIdData);
                    FillAlarmData(algData, alarmUnit);
                    trackIdData.history.clear();
                    trackIdData.historyFull = false;
                }
            }
        }
    }
    if (haveRecRst) {
        m_overviewRecInst.OverviewRecordFrame(recSensitity);
    }
}

void Sensitivity::FillAlarmData(AlgDataPtr algData, DataAlarmUnit& alarmUnit) {
    if (!algData->taskDataAlarm.alarmData) {
        algData->taskDataAlarm.alarmData = std::make_shared<DataAlarm>();
    }

    auto alarmData = algData->taskDataAlarm.alarmData;
    if (!alarmData) {
        LOG_WARN("{}[{}] NEED ALARM But No MEM", kTag, task_id);
        return;
    }
    need_alarm = true;
    alarmData->alarms.push_back(alarmUnit);
}

void Sensitivity::FillAlarmDataTrackId(DataAlarmUnit& alarmUnit, TrackIdData& trackIdData) {
    for (auto& history : trackIdData.history) {
        DataAlarmTargetConfidence targetConf;
        targetConf.box       = history.targetCondidenceInfo.box;
        targetConf.targetPos = history.targetCondidenceInfo.targetPos;
        alarmUnit.box        = history.targetCondidenceInfo.box;
        alarmUnit.friends    = history.targetCondidenceInfo.friends;
        alarmUnit.targetHistory.push_back(targetConf);
    }

    if (alarmUnit.friends.empty()) {
        alarmUnit.boxs.push_back(alarmUnit.box);
    } else {
        alarmUnit.boxs = alarmUnit.friends;
    }

    alarmUnit.matchInfo = trackIdData.target.matchInfo;
    for (auto& relatedEl : trackIdData.target.relatedEls) {
        alarmUnit.boxs.push_back(relatedEl.box);
    }

    alarmUnit.trackId    = static_cast<int>(trackIdData.trackId);
    alarmUnit.strTrackId = trackIdData.trackIdUuid;
    alarmUnit.matchInfo  = trackIdData.target.matchInfo;
}

void Sensitivity::OldTrackId() {
    for (auto it = m_mapTrackIdStatus.begin(); it != m_mapTrackIdStatus.end();) {
        if (!it->second.detected) {
            LOG_AITARGET("{}[{}] trackId {} Disappear", kTag, task_id, it->second.trackId);
            it = m_mapTrackIdStatus.erase(it);
        } else {
            it->second.detected = false;
            ++it;
        }
    }
}

void Sensitivity::HandTrackData(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    if (AlgDataType::TaskDataFriendDistance == input->dataType) {
        AddGroupHistory(input, algData->firstTimePoint);
    } else {
        AddHistory(input, algData->firstTimePoint);
    }

    OldHistory();

    CalcSensitityWithArea(algData);
    OldTrackId();
}

}  // namespace cosmo
