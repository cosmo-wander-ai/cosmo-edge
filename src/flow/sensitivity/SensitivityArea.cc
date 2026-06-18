// SensitivityArea.cc — Area-based sensitivity calculation for Sensitivity.
// Split from Sensitivity.cc to reduce file size (DEBT-007).

#include <algorithm>

#include "flow/common/AlgDataRecord.h"
#include "flow/sensitivity/SensitivityTypes.h"
#include "util/Keys.h"
#include "util/Log.h"

static constexpr const char* kTag = "Sensitivity ";
namespace cosmo {

void Sensitivity::AddTargetToAreaTarget(const std::string& areaId, AreaIdDataEl& areaData,
                                        DataDetTrackClassifyPtr input) {
    for (auto& target : input->targets) {
        if (target.bFilter) {
            continue;
        }
        if (!target.areaSign.shielded_areas.empty()) {
            continue;
        }

        // No area
        if (key::DEFAULT_AREA == areaId) {
            if (m_haveClassify) {  // Has classification, need to judge logic result
                if (target.bLogicResult) {
                    areaData.bResult = true;
                    areaData.targets.push_back(target);
                }
            }
            continue;
        }
        for (auto targetArea : target.areaSign.areas) {
            if (targetArea.area_id != areaId) {
                continue;
            }
            if (m_haveClassify) {  // Has classification, need to judge logic result
                if (target.bLogicResult) {
                    areaData.bResult = true;
                    areaData.targets.push_back(target);
                }
            } else {  // Only detection, add directly to result
                areaData.bResult = true;
                areaData.targets.push_back(target);
            }
            break;
        }
    }
}

void Sensitivity::AddAreaTargetHistory(DataDetTrackClassifyPtr input,
                                       const std::chrono::steady_clock::time_point& dataTimePoint) {
    auto areas = GetAreas();
    for (auto& area : areas) {
        auto& areaTargetData = m_mapAreaTargetStatus[area.areaId];
        areaTargetData.id    = area.areaId;
        areaTargetData.name  = area.name;
        AreaIdDataEl areaData;
        areaData.timePoint = dataTimePoint;
        AddTargetToAreaTarget(area.areaId, areaData, input);
        areaTargetData.history.push_back(areaData);
    }
    if (areas.empty()) {
        auto& areaTargetData = m_mapAreaTargetStatus[std::string(key::DEFAULT_AREA)];
        areaTargetData.id    = std::string(key::DEFAULT_AREA);
        areaTargetData.name  = std::string(key::DEFAULT_AREA_NAME);
        AreaIdDataEl areaData;
        areaData.timePoint = dataTimePoint;
        AddTargetToAreaTarget(areaTargetData.id, areaData, input);
        areaTargetData.history.push_back(areaData);
    }
}

void Sensitivity::OldAreaTargetHistory() {
    for (auto it = m_mapAreaTargetStatus.begin(); it != m_mapAreaTargetStatus.end();) {
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

void Sensitivity::CalcAreaTargetSensitity(AlgDataPtr algData) {
    auto paramRatio = m_params.sensitivityRatio;
    MsgRecSensitity recSensitity;
    bool haveRecRst          = false;
    recSensitity.index       = frame_index;
    recSensitity.streamIndex = stream_index;
    recSensitity.timestamp   = timestamp;
    for (auto it = m_mapAreaTargetStatus.begin(); it != m_mapAreaTargetStatus.end(); ++it) {
        AreaTargetData& areaTargetData = it->second;
        if (!areaTargetData.historyFull) {  // Queue not full, cannot calculate sensitivity
            continue;
        }
        std::string defaultArea = std::string(key::DEFAULT_AREA);  // No area state
        size_t totalCount       = it->second.history.size();
        size_t detectCount      = 0;
        auto duration           = std::chrono::duration_cast<std::chrono::milliseconds>(
                            it->second.history.back().timePoint - it->second.history.front().timePoint)
                            .count();
        detectCount = std::count_if(areaTargetData.history.begin(), areaTargetData.history.end(),
                                    [](const auto& el) { return el.bResult; });

        if (SensitivityType::kFixCount == m_params.sensitivityType) {
            totalCount = m_params.senTotalCount;
        }

        // The last time needs to be positive detection
        if ((totalCount > 0) && (it->second.history.back().bResult)) {
            haveRecRst = true;
            RecAreaTargetData(recSensitity, paramRatio, areaTargetData);
            float ratio = static_cast<float>(detectCount) / totalCount;
            if (ratio >= paramRatio) {
                std::string area;
                if (it->first != defaultArea) {
                    area = it->first;
                }

                DataAlarmUnit alarmUnit;
                if (AlgDataType::TaskDataClassifyMultPic == algData->dataType) {
                    alarmUnit.baseFrame = algData->taskDataClassifyMultPic.baseFrame;
                }
                alarmUnit.flowActionId = action_node.flowActionId;
                alarmUnit.reportType   = OnEventsReportType::Trigger;
                alarmUnit.areaId       = area;
                alarmUnit.areaName     = it->second.name;
                for (auto& target : it->second.history.back().targets) {
                    util::Box box = target.box;
                    alarmUnit.boxs.push_back(box);
                    alarmUnit.box       = target.box;
                    alarmUnit.matchInfo = target.matchInfo;
                }
                LOG_INFO(
                    "{}[{}] [NEED ALARM] In Area:{}/{} Detect:{} Total:{} At Frame:{} sensitivity:{} "
                    "ratio:{} paramRatio:{} detDuration:{} targetCount:{}",
                    kTag, task_id, area, it->second.name, detectCount, totalCount, frame_index,
                    m_params.sensitivity, ratio, paramRatio, duration,
                    it->second.history.back().targets.size());
                FillAlarmData(algData, alarmUnit);
                areaTargetData.history.clear();
                areaTargetData.historyFull = false;
            }
        }
    }
    if (haveRecRst) {
        m_overviewRecInst.OverviewRecordFrame(recSensitity);
    }
}

void Sensitivity::HandAreaTargetData(AlgDataPtr algData, DataDetTrackClassifyPtr input) {
    AddAreaTargetHistory(input, algData->firstTimePoint);
    OldAreaTargetHistory();
    CalcAreaTargetSensitity(algData);
}

void Sensitivity::AddAreaThingsHistory(AlgDataPtr algData,
                                       const std::chrono::steady_clock::time_point& dataTimePoint) {
    std::vector<AlgTaskDataRecogThings> detAreas;
    if (AlgDataType::TaskDataRecognizer == algData->dataType) {
        detAreas = algData->taskDatarecog.areas;
    } else if (AlgDataType::TaskDataAiFilter == algData->dataType) {
        if (algData->taskAiFilter.bInputArea) {
            detAreas = algData->taskAiFilter.areas;
        }
    }
    for (auto& area : detAreas) {
        auto& areaTargetData = m_mapAreaTargetStatus[area.areaId];
        areaTargetData.id    = area.areaId;
        areaTargetData.name  = area.areaName;
        AreaIdDataEl areaData;
        areaData.timePoint = dataTimePoint;
        areaData.bResult   = area.bLogicResult;
        AiDetectRstEl target;
        target.box       = area.box;
        target.matchInfo = area.matchInfo;
        areaData.targets.push_back(target);
        areaTargetData.history.push_back(areaData);
    }
}

void Sensitivity::HandAreaThingsData(AlgDataPtr algData) {
    // Reuse object no sensitivity alarm structure, only AddAreaThingsHistory is different
    AddAreaThingsHistory(algData, algData->firstTimePoint);
    OldAreaTargetHistory();
    CalcAreaTargetSensitity(algData);
}

void Sensitivity::RecTrackTargetDataAreaAddTarget(MsgRecArea& recArea, int trackId, float expertSensitity,
                                                  TargetSensitivityDetCount& targetCalc) {
    MsgRecSensitityTarget target;
    target.trackId         = trackId;
    target.queIsFull       = (targetCalc.demCount == targetCalc.totalCount) ? true : false;
    target.dem             = targetCalc.demCount;
    target.sensitity       = static_cast<float>(targetCalc.detectCount) / targetCalc.demCount;
    target.rsts            = targetCalc.rsts;
    target.expertSensitity = expertSensitity;
    recArea.targets.push_back(target);
}

void Sensitivity::RecTrackTargetData(MsgRecSensitity& recSensitity, const std::string& areaId, int trackId,
                                     float expertSensitity, TargetSensitivityDetCount& targetCalc) {
    auto iter = find_if(recSensitity.areas.begin(), recSensitity.areas.end(),
                        [&](const MsgRecArea& element) { return areaId == element.areaId; });
    if (iter != recSensitity.areas.end()) {
        RecTrackTargetDataAreaAddTarget(*iter, trackId, expertSensitity, targetCalc);
    } else {
        MsgRecArea recArea;
        recArea.areaId = areaId;
        RecTrackTargetDataAreaAddTarget(recArea, trackId, expertSensitity, targetCalc);
        recSensitity.areas.push_back(recArea);
    }
}

void Sensitivity::RecAreaTargetData(MsgRecSensitity& recSensitity, float expSensitity,
                                    AreaTargetData& areaTargetData) {
    MsgRecArea recArea;
    recArea.areaId = areaTargetData.id;

    MsgRecSensitityTarget recTarget;
    recTarget.queIsFull = areaTargetData.historyFull;
    std::transform(areaTargetData.history.begin(), areaTargetData.history.end(),
                   std::back_inserter(recTarget.rsts),
                   [](const auto& targetHistory) { return targetHistory.bResult; });
    size_t targetAvtiveCount = std::count_if(areaTargetData.history.begin(), areaTargetData.history.end(),
                                             [](const auto& targetHistory) { return targetHistory.bResult; });

    if ((recTarget.queIsFull) && (!recTarget.rsts.empty())) {
        recTarget.sensitity = float(targetAvtiveCount) / recTarget.rsts.size();
    }

    recTarget.expertSensitity = expSensitity;
    recArea.targets.push_back(recTarget);

    recSensitity.areas.push_back(recArea);
}

}  // namespace cosmo
