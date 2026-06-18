// AiDetectorTarget.cc — Target area marking, overview recording, and history for AiDetector.
// Split from AiDetector.cc to reduce file size (DEBT-007).

#include "flow/common/AlgDataRecord.h"
#include "flow/detect/AiDetector.h"
#include "util/GeometricPos.h"

static constexpr const char* kTag = "AI-DETECTER ";
namespace cosmo {

void AiDetector::TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type,
                               MsgTaskArea& area, int picWidth, int picHeight, bool bAssociatedArea,
                               const std::string& mainAreaId) {
    if (BoxInArea(target.box, pos, target.point, area.points, picWidth, picHeight)) {
        TargetAreaUnit targetArea;
        targetArea.area_id            = area.areaId;
        targetArea.area_name          = area.name;
        targetArea.is_associated_area = bAssociatedArea;
        targetArea.main_area_id       = mainAreaId;
        targetArea.position           = pos;
        targetArea.type               = type;
        target.targetPos              = pos;
        if (TargetAreaType::kNormal == type) {
            target.areaSign.areas.push_back(targetArea);
        } else {
            target.areaSign.shielded_areas.push_back(targetArea);
        }
    }
    return;
}

void AiDetector::TargetAddLine(AiDetectRstEl& target, TargetPosition pos, MsgTaskArea& area, int picWidth,
                               int picHeight, bool bAssociatedArea, const std::string& mainAreaId) {
    TargetLineUnit targetonLineUnit;
    targetonLineUnit.area_id            = area.areaId;
    targetonLineUnit.area_name          = area.name;
    targetonLineUnit.is_associated_area = bAssociatedArea;
    targetonLineUnit.main_area_id       = mainAreaId;
    targetonLineUnit.position           = pos;
    targetonLineUnit.type = BoxOnLinePos(target.box, pos, target.point, area.linePoints, picWidth, picHeight);
    target.targetPos      = pos;
    target.areaSign.lines.push_back(targetonLineUnit);

    return;
}

// Mark area information on targets
void AiDetector::SignTargetAreas(AlgDataPtr dataPtr, const std::string& taskId) {
    if (!dataPtr) {
        return;
    }

    if (!dataPtr->chanDataDec.frame) {
        return;
    }

    if (!dataPtr->chanDataDetect.detRet) {
        return;
    }

    auto detRet = dataPtr->chanDataDetect.detRet;
    auto width  = dataPtr->chanDataDec.frame->GetWidth();
    auto height = dataPtr->chanDataDec.frame->GetHeight();
    std::shared_lock<std::shared_mutex> lock(mtx);
    auto areaIt = m_taskAreas.find(taskId);
    if (areaIt == m_taskAreas.end())
        return;  // Task deleted, skip area marking
    auto& taskArea = areaIt->second;
    if (taskArea.areas.size()) {
        detRet->bHaveArea = true;
    }
    if (taskArea.shieldedAreas.size()) {
        detRet->bHaveShieldedArea = true;
    }

    auto labelParam = GetTaskLabelParams(taskId);

    for (auto& target : detRet->targets) {
        auto pos = GetTaskLabelPos(target.confidence.label, labelParam);
        for (auto area : taskArea.areas) {
            if (!area.points.empty()) {
                TargetAddArea(target, pos, TargetAreaType::kNormal, area, width, height, false, "");
            }
            if (!area.linePoints.empty()) {
                TargetAddLine(target, pos, area, width, height, false, "");
            }
            // Associated areas
            for (auto assArea : area.associatedAreas) {
                if (!assArea.points.empty()) {
                    TargetAddArea(target, pos, TargetAreaType::kNormal, assArea, width, height, true,
                                  area.areaId);
                }
                if (!assArea.linePoints.empty()) {
                    TargetAddLine(target, pos, assArea, width, height, true, area.areaId);
                }
            }
        }

        for (auto area : taskArea.shieldedAreas) {
            if (!area.points.empty()) {
                TargetAddArea(target, pos, TargetAreaType::kShield, area, width, height, false, "");
            }
        }
    }
}

void AiDetector::RecordHistory(AlgDataPtr dataPtr, const std::string& taskId) {
    if (!dataPtr)
        return;
    if (!dataPtr->chanDataDetect.detRet)
        return;

    std::lock_guard<std::shared_mutex> lock(mtx);
    auto it = m_taskHistorys.find(taskId);
    if (it == m_taskHistorys.end())
        return;  // Task deleted, skip recording
    auto& taskHistory = it->second;
    taskHistory.push_back(*dataPtr->chanDataDetect.detRet);
    if (dataPtr->chanDataDetect.detRet->timestamp - taskHistory.front().timestamp >
        media::kVideoInfoMaxDuration) {
        taskHistory.pop_front();
    }
}

std::vector<DataDetTrackClassify> AiDetector::GetHistory(const std::string& /*channelId*/,
                                                         const std::string& taskId, int64_t from, int64_t ts,
                                                         int64_t to) {
    std::vector<DataDetTrackClassify> rst;
    std::lock_guard<std::shared_mutex> lock(mtx);
    auto histIt = m_taskHistorys.find(taskId);
    if (histIt == m_taskHistorys.end())
        return rst;
    const auto& taskHistory = histIt->second;
    bool bStart             = false;
    for (auto historyEl : taskHistory) {
        auto timestampDiff = (ts == 0) ? 0 : abs(ts - historyEl.timestamp);
        if ((from <= historyEl.frameIndex) && (timestampDiff < media::kTimestampDiff)) {
            bStart = true;
        }

        if (bStart) {
            historyEl.dataType = AlgDataType::ChannelDataDetect;
            rst.push_back(historyEl);
            if (to <= historyEl.frameIndex) {
                break;
            }
        }
    }

    return rst;
}

void AiDetector::AddOverviewTask(const std::string& taskId) {
    auto it = m_overviewRecInsts.find(taskId);
    if (it == m_overviewRecInsts.end()) {
        OverviewRecordAiRstPtr recordInst = std::make_shared<OverviewRecordAiRst>(taskId, "detect_" + m_name);
        m_overviewRecInsts[taskId]        = recordInst;
    }
}

void AiDetector::OverviewRecord(const std::string& taskId, DataDetTrackClassifyPtr detRet) {
    auto it = m_overviewRecInsts.find(taskId);
    if (it != m_overviewRecInsts.end() && it->second) {
        it->second->OverviewRecordFrame(detRet);
    }
}

MsgOverviewMem AiDetector::GetOverviewInfo(const std::string& /*channelId*/, const std::string& taskId,
                                           int64_t streamIndex, int64_t from, int64_t to) {
    MsgOverviewMem info;
    auto it = m_overviewRecInsts.find(taskId);
    if (it != m_overviewRecInsts.end() && it->second) {
        info = it->second->GetOverviewInfo(streamIndex, from, to);
    }

    return info;
}

}  // namespace cosmo
