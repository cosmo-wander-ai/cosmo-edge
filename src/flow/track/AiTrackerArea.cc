// AiTrackerArea.cc — Target area marking and break-line detection for AiTracker.
// Split from AiTracker.cc to reduce file size (DEBT-007).

#include <algorithm>

#include "flow/common/AlgDataRecord.h"
#include "flow/track/AiTracker.h"
#include "flow/track/AiTrackerTypes.h"
#include "util/GeometricCalculation.h"
#include "util/GeometricPos.h"
#include "util/Log.h"
#include "util/UuidUtil.h"

static constexpr const char* kTag = "AI-TRACKER ";
namespace cosmo {

TargetPosition AiTracker::GetTaskLabelPos(const std::string& label,
                                          const std::vector<AiLabelParam>& labelParams) {
    auto it = std::find_if(labelParams.begin(), labelParams.end(),
                           [&label](const auto& lp) { return lp.label == label; });
    if (it != labelParams.end()) {
        return it->pos;
    }

    return TargetPosition::kBottom;  // Default
}

void AiTracker::TargetAddArea(AiDetectRstEl& target, TargetPosition pos, TargetAreaType type,
                              const MsgTaskArea& area, bool bAssociatedArea, const std::string& mainAreaId) {
    if ((pic_width_ <= 0) || (pic_height_ <= 0)) {
        return;
    }
    if (BoxInArea(target.box, pos, target.point, area.points, pic_width_, pic_height_)) {
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

void AiTracker::TargetAddLine(AiDetectRstEl& target, TargetPosition pos, const MsgTaskArea& area,
                              bool bAssociatedArea, const std::string& mainAreaId) {
    if ((pic_width_ <= 0) || (pic_height_ <= 0)) {
        return;
    }
    TargetLineUnit targetonLineUnit;
    targetonLineUnit.area_id            = area.areaId;
    targetonLineUnit.area_name          = area.name;
    targetonLineUnit.is_associated_area = bAssociatedArea;
    targetonLineUnit.main_area_id       = mainAreaId;
    targetonLineUnit.position           = pos;
    targetonLineUnit.type =
        BoxOnLinePos(target.box, pos, target.point, area.linePoints, pic_width_, pic_height_);
    target.targetPos = pos;
    target.areaSign.lines.push_back(targetonLineUnit);

    return;
}
// Mark area info on target
void AiTracker::SignTargetAreas(AlgDataPtr dataPtr) {
    if (!dataPtr) {
        return;
    }

    if (!dataPtr->chanDataDec.frame) {
        return;
    }

    if (!dataPtr->GetTaskResult(AlgDataType::TaskDataTrack)) {
        return;
    }

    auto detRet = dataPtr->GetTaskResult(AlgDataType::TaskDataTrack);
    pic_width_  = dataPtr->chanDataDec.frame->GetWidth();
    pic_height_ = dataPtr->chanDataDec.frame->GetHeight();
    std::shared_lock<std::shared_mutex> lock(mtx);
    if (task_area_.areas.size()) {
        detRet->bHaveArea = true;
    }
    if (task_area_.shieldedAreas.size()) {
        detRet->bHaveShieldedArea = true;
    }

    for (auto& target : detRet->targets) {
        auto pos = GetTaskLabelPos(target.confidence.label, label_param_);
        for (auto area : task_area_.areas) {
            if (!area.points.empty()) {
                TargetAddArea(target, pos, TargetAreaType::kNormal, area, false, "");
            }
            if (!area.linePoints.empty()) {
                TargetAddLine(target, pos, area, false, "");
            }
            // Associated area
            for (auto assArea : area.associatedAreas) {
                if (!assArea.points.empty()) {
                    TargetAddArea(target, pos, TargetAreaType::kNormal, assArea, true, area.areaId);
                }
                if (!assArea.linePoints.empty()) {
                    TargetAddLine(target, pos, assArea, true, area.areaId);
                }
            }
        }

        for (auto shieldedArea : task_area_.shieldedAreas) {
            if (!shieldedArea.points.empty()) {
                TargetAddArea(target, pos, TargetAreaType::kShield, shieldedArea, false, "");
            }
            // Associated area
            for (auto assArea : shieldedArea.associatedAreas) {
                if (!assArea.points.empty()) {
                    TargetAddArea(target, pos, TargetAreaType::kShield, assArea, true, shieldedArea.areaId);
                }
                if (!assArea.linePoints.empty()) {
                    TargetAddLine(target, pos, assArea, true, shieldedArea.areaId);
                }
            }
        }
    }

    return;
}

void AiTracker::TargetAddHistory(AlgDataPtr algData) {
    if (!algData->GetTaskResult(AlgDataType::TaskDataTrack)) {
        return;
    }

    for (auto& target : algData->GetTaskResult(AlgDataType::TaskDataTrack)->targets) {
        auto& idData = map_track_id_status_[target.trackId];
        if (idData.track_id != target.trackId) {
            idData.track_id_info = util::GenerateUUID();
            idData.track_id      = target.trackId;
            LOG_AITARGET("{}[{} {}] TrackId {}/{} Appear At Frame:{}", kTag, name_, uuid, target.trackId,
                         idData.track_id_info, frame_index_);
        }

        if (target.box.height > 0) {
            idData.hw_ratios.push(target.hwRatio);
        }
        while (idData.hw_ratios.size() > static_cast<size_t>(param_.motion.frames)) {
            idData.hw_ratios.pop();
        }
        target.hwRatioVariation = CalculateNormalizedVariation(idData.hw_ratios);
        if (target.hwRatioVariation > shape_change_threshold_) {
            target.shapeChangeStatus = AIShapeChangeState::CHANGE;
        } else {
            target.shapeChangeStatus = AIShapeChangeState::STILL;
        }

        target.trackIdInfo = idData.track_id_info;
        idData.detected    = true;
        idData.det_el      = target;
    }
}

void AiTracker::TargetOldHistory(AlgDataPtr /*algData*/) {
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end();) {
        if (!it->second.detected) {
            it = map_track_id_status_.erase(it);
        } else {
            it->second.detected = false;
            ++it;
        }
    }
}

TargetLineUnit AiTracker::TargetOnLinePos(const AiDetectRstEl& target, const std::string& areaId) {
    auto it = std::find_if(target.areaSign.lines.begin(), target.areaSign.lines.end(),
                           [&areaId](const auto& line) { return line.area_id == areaId; });
    if (it != target.areaSign.lines.end()) {
        return *it;
    }

    return {};
}

void AiTracker::TargetCalcBreakLineArea(TrackIdData& idData, const MsgTaskArea& area) {
    auto& targetLineData           = idData.line_datas[area.areaId];
    targetLineData.break_line_type = TargetBreakLineType::kNone;
    if (idData.det_el.bFilter) {
        return;
    }
    // Enter shielded area
    if (!idData.det_el.areaSign.shielded_areas.empty()) {
        targetLineData.target_pos_now = TargetPositionOnLineType::kUnknown;
        LOG_AITARGET("{}[{} {}] TrackId {} Entered shielded area", kTag, name_, uuid, idData.track_id);
        return;
    }

    for (auto& assArea : area.associatedAreas) {
        TargetCalcBreakLineArea(idData, assArea);
    }

    // Status of the latest point
    auto targetInfo   = TargetOnLinePos(idData.det_el, area.areaId);
    auto targetPosNow = targetInfo.type;
    if (TargetPositionOnLineType::kOnLine == targetPosNow) {
        // Point is on the line, does not cross
        return;
    }

    // Latest point has same relative position as the last point
    if (targetLineData.target_pos_now == targetPosNow) {
        return;
    }

    if (TargetPositionOnLineType::kLeft == targetPosNow) {
        auto point = GetMsgPointFromRect(idData.det_el.box, targetInfo.position, idData.det_el.point,
                                         pic_width_, pic_height_);
        LOG_AITARGET(
            "{}[{} {}] TrackId {} on line {}/{} left side of, point:{:02f}.{:02f} {}.{} {}x{} {} {}x{} {} {}",
            kTag, name_, uuid, idData.track_id, area.areaId, area.name, point.x, point.y, idData.det_el.box.x,
            idData.det_el.box.y, idData.det_el.box.width, idData.det_el.box.height, targetInfo.position,
            pic_width_, pic_height_, idData.det_el.motionStatus, idData.det_el.trackStatus);
    }
    if (TargetPositionOnLineType::kRight == targetPosNow) {
        auto point = GetMsgPointFromRect(idData.det_el.box, targetInfo.position, idData.det_el.point,
                                         pic_width_, pic_height_);
        LOG_AITARGET(
            "{}[{} {}] TrackId {} on line {}/{} right side of, point:{:02f}.{:02f} {}.{} {}x{} {} {}x{} {} "
            "{}",
            kTag, name_, uuid, idData.track_id, area.areaId, area.name, point.x, point.y, idData.det_el.box.x,
            idData.det_el.box.y, idData.det_el.box.width, idData.det_el.box.height, targetInfo.position,
            pic_width_, pic_height_, idData.det_el.motionStatus, idData.det_el.trackStatus);
    }

    // Latest detected point is in shielded area or last point is in shielded area
    if ((TargetPositionOnLineType::kUnknown == targetPosNow) ||
        (TargetPositionOnLineType::kUnknown == targetLineData.target_pos_now)) {
        // Crossing status remains unchanged
        targetLineData.target_pos_now = targetPosNow;
        targetLineData.det_el         = idData.det_el;
        return;
    }

    bool direction = false;
    if (TargetPositionOnLineType::kLeft == targetLineData.target_pos_now) {
        direction = true;  // Unidirectional line crossing forward
    }
    if (DirectionType::DirectionTypeTwoWay == area.iderectionType) {
        direction = true;  // Bidirectional line always crosses
    }
    // Crossing line
    // Determine intersection
    const auto& linePoints = area.linePoints;
    auto point1            = GetMsgPointFromRect(targetLineData.det_el.box, targetInfo.position,
                                                 targetLineData.det_el.point, pic_width_, pic_height_);
    auto point2 = GetMsgPointFromRect(idData.det_el.box, targetInfo.position, idData.det_el.point, pic_width_,
                                      pic_height_);
    for (size_t j = 1; j < linePoints.size(); ++j) {
        if ((std::max(point1.x, point2.x) >= std::min(linePoints[j - 1].x, linePoints[j].x) &&
             std::min(point1.x, point2.x) <=
                 std::max(linePoints[j - 1].x, linePoints[j].x)) &&  // Check x-axis projection
            (std::max(point1.y, point2.y) >= std::min(linePoints[j - 1].y, linePoints[j].y) &&
             std::min(point1.y, point2.y) <=
                 std::max(linePoints[j - 1].y, linePoints[j].y))  // Check y-axis projection
        ) {
            auto bIntersection = util::LineIntersection(point1, point2, linePoints[j - 1], linePoints[j]);
            if (bIntersection <= 0)  // Segment intersection and point on segment both count as crossing,
                                     // Point on line - since detection is line-segment by line-segment,
                                     // crossing exactly on the line is possible
            {
                LOG_AITARGET("{}[{} {}] TrackId {} {}crossed line {}/{} At {}", kTag, name_, uuid,
                             idData.track_id, direction ? "forward" : "reverse", area.areaId, area.name,
                             frame_index_);
                if (direction)
                    targetLineData.break_line_type = TargetBreakLineType::kPos;
                else
                    targetLineData.break_line_type = TargetBreakLineType::kNeg;
            } else {
                LOG_AITARGET("{}[{} {}] TrackId {} no intersection", kTag, name_, uuid, idData.track_id);
            }
        } else {
            LOG_AITARGET("{}[{} {}] TrackId {} projection mismatch", kTag, name_, uuid, idData.track_id);
        }
    }
    targetLineData.target_pos_now = targetPosNow;
    targetLineData.det_el         = idData.det_el;
    // targetLineData.break_line_type = TargetBreakLineType::kNone;
}

void AiTracker::TargetCalcBreakLine(AlgDataPtr /*algData*/) {
    if (!area_have_line_) {
        return;
    }

    std::shared_lock<std::shared_mutex> lock(mtx);  // Lock task_area_
    for (auto it = map_track_id_status_.begin(); it != map_track_id_status_.end(); it++) {
        for (auto& area : task_area_.areas) {
            auto& idData = it->second;
            if (idData.det_el.bFilter) {
                continue;
            }
            TargetCalcBreakLineArea(idData, area);
        }
    }
}

// Copy crossing status to data stream
void AiTracker::CpBreakLineStatusToData(AlgDataPtr algData) {
    if (!area_have_line_) {
        return;
    }

    for (auto& target : algData->GetTaskResult(AlgDataType::TaskDataTrack)->targets) {
        auto& idData = map_track_id_status_[target.trackId];

        for (auto& linestatus : target.areaSign.lines) {
            auto& targetLineData = idData.line_datas[linestatus.area_id];
            linestatus.status    = targetLineData.break_line_type;
        }
    }
}
}  // namespace cosmo
