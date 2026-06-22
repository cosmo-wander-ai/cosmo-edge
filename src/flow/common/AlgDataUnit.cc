// AlgDataUnit — Asynchronous Queue - Shim Header

#include "flow/common/AlgDataUnit.h"

#include <algorithm>

#include "util/GeometricPos.h"
#include "util/Log.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo {
DataDetTrackClassifyPtr AlgDataCopyDetTrackData(DataDetTrackClassifyPtr input) {
    if (!input)
        return nullptr;

    DataDetTrackClassifyPtr detData = std::make_shared<DataDetTrackClassify>();

    detData->bHaveArea         = input->bHaveArea;
    detData->bHaveShieldedArea = input->bHaveShieldedArea;
    detData->streamIndex       = input->streamIndex;
    detData->frameIndex        = input->frameIndex;
    detData->timestamp         = input->timestamp;
    detData->picWidth          = input->picWidth;
    detData->picHeight         = input->picHeight;
    detData->dataType          = input->dataType;
    detData->targets           = input->targets;
    detData->groupTargets      = input->groupTargets;
    detData->reportType        = input->reportType;
    detData->areaInfo          = input->areaInfo;

    return detData;
}

DataAlarmPtr AlgDataCopyAlarmData(DataAlarmPtr input) {
    if (!input)
        return nullptr;

    DataAlarmPtr alarmData = std::make_shared<DataAlarm>();

    alarmData->type         = input->type;
    alarmData->flowActionId = input->flowActionId;
    alarmData->multiAlarms  = input->multiAlarms;
    alarmData->alarms       = input->alarms;

    return alarmData;
}

AlgDataPtr AlgDataCopy(AlgDataPtr input) {
    if (!input)
        return nullptr;

    AlgDataPtr algData = std::make_shared<AlgData>();

    algData->dataType       = input->dataType;
    algData->channelId      = input->channelId;
    algData->taskId         = input->taskId;
    algData->bHaveTrack     = input->bHaveTrack;
    algData->bHaveRelated   = input->bHaveRelated;
    algData->bHaveClassify  = input->bHaveClassify;
    algData->bHaveLandmark  = input->bHaveLandmark;
    algData->bHaveLogic     = input->bHaveLogic;
    algData->firstTimePoint = input->firstTimePoint;

    algData->chanDataOrig = input->chanDataOrig;
    algData->chanDataDec  = input->chanDataDec;

    algData->legacyDetect.atomicCode  = input->legacyDetect.atomicCode;
    algData->legacyDetect.lables      = input->legacyDetect.lables;
    algData->legacyDetect.atomicCodes = input->legacyDetect.atomicCodes;
    algData->legacyDetect.detRet      = AlgDataCopyDetTrackData(input->legacyDetect.detRet);

    algData->chanDataDetect.atomicCode  = input->chanDataDetect.atomicCode;
    algData->chanDataDetect.lables      = input->chanDataDetect.lables;
    algData->chanDataDetect.atomicCodes = input->chanDataDetect.atomicCodes;
    algData->chanDataDetect.detRet      = AlgDataCopyDetTrackData(input->chanDataDetect.detRet);

    // Uniformly copy all entries in the taskResults map
    for (auto& [type, ptr] : input->taskResults) {
        algData->SetTaskResult(type, AlgDataCopyDetTrackData(ptr));
    }

    algData->taskAiFilter.bInputArea = input->taskAiFilter.bInputArea;
    algData->taskAiFilter.areas      = input->taskAiFilter.areas;
    algData->taskAiFilter.targetRst  = AlgDataCopyDetTrackData(input->taskAiFilter.targetRst);

    algData->taskDataClassifyMultPic.baseFrame = input->taskDataClassifyMultPic.baseFrame;
    algData->taskDataClassifyMultPic.classifyRst =
        AlgDataCopyDetTrackData(input->taskDataClassifyMultPic.classifyRst);

    algData->taskDatarecog = input->taskDatarecog;

    algData->taskDataAlarm.alarmData = AlgDataCopyAlarmData(input->taskDataAlarm.alarmData);

    return algData;
}

TargetPosition GetLabelPos(std::string& label, std::vector<AiLabelParam>& labelParams) {
    auto it = std::find_if(labelParams.begin(), labelParams.end(),
                           [&label](const auto& labelEl) { return labelEl.label == label; });
    if (it != labelParams.end()) {
        return it->pos;
    }

    return TargetPosition::kBottom;  // Default
}

void TargetAddAreas(AiDetectRstEl& target, int picWidth, int picHeight, TargetPosition pos,
                    TargetAreaType type, const MsgTaskArea& area, bool bAssociatedArea,
                    const std::string& mainAreaId) {
    if ((picWidth <= 0) || (picHeight <= 0)) {
        return;
    }
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

void TargetAddLines(AiDetectRstEl& target, int picWidth, int picHeight, TargetPosition pos,
                    const MsgTaskArea& area, bool bAssociatedArea, const std::string& mainAreaId) {
    if ((picWidth <= 0) || (picHeight <= 0)) {
        return;
    }
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

void TargetSignAreas(DataDetTrackClassifyPtr detRet, const std::vector<MsgTaskArea>& inAreas,
                     const std::vector<MsgTaskArea>& inShieldedAreas, std::vector<AiLabelParam>& labelPos) {
    if (!detRet) {
        return;
    }

    if (inAreas.size()) {
        detRet->bHaveArea = true;
    }
    if (inShieldedAreas.size()) {
        detRet->bHaveShieldedArea = true;
    }

    for (auto& target : detRet->targets) {
        auto pos = GetLabelPos(target.confidence.label, labelPos);
        for (const auto& area : inAreas) {
            if (!area.points.empty()) {
                TargetAddAreas(target, detRet->picWidth, detRet->picHeight, pos, TargetAreaType::kNormal,
                               area, false, "");
            }
            if (!area.linePoints.empty()) {
                TargetAddLines(target, detRet->picWidth, detRet->picHeight, pos, area, false, "");
            }
            // Associated area
            for (const auto& assArea : area.associatedAreas) {
                if (!assArea.points.empty()) {
                    TargetAddAreas(target, detRet->picWidth, detRet->picHeight, pos, TargetAreaType::kNormal,
                                   assArea, true, area.areaId);
                }
                if (!assArea.linePoints.empty()) {
                    TargetAddLines(target, detRet->picWidth, detRet->picHeight, pos, assArea, true,
                                   area.areaId);
                }
            }
        }

        for (const auto& shieldedArea : inShieldedAreas) {
            if (!shieldedArea.points.empty()) {
                TargetAddAreas(target, detRet->picWidth, detRet->picHeight, pos, TargetAreaType::kShield,
                               shieldedArea, false, "");
            }
            // Associated area
            for (const auto& assArea : shieldedArea.associatedAreas) {
                if (!assArea.points.empty()) {
                    TargetAddAreas(target, detRet->picWidth, detRet->picHeight, pos, TargetAreaType::kShield,
                                   assArea, true, shieldedArea.areaId);
                }
                if (!assArea.linePoints.empty()) {
                    TargetAddLines(target, detRet->picWidth, detRet->picHeight, pos, assArea, true,
                                   shieldedArea.areaId);
                }
            }
        }
    }

    return;
}

std::vector<std::pair<util::Point, util::Point>> GetAreaOsdLines(MsgTaskArea area, int width, int height) {
    std::vector<std::pair<util::Point, util::Point>> lines;
    for (auto assArea : area.associatedAreas) {
        auto assLines = GetAreaOsdLines(assArea, width, height);
        if (!assLines.empty())
            lines.insert(lines.end(), assLines.begin(), assLines.end());
    }

    if (!area.points.empty()) {
        for (size_t index = 1; index < area.points.size(); index++) {
            std::pair<util::Point, util::Point> line;
            line.first.x  = static_cast<int>(area.points[index - 1].x * width);
            line.first.y  = static_cast<int>(area.points[index - 1].y * height);
            line.second.x = static_cast<int>(area.points[index].x * width);
            line.second.y = static_cast<int>(area.points[index].y * height);
            lines.push_back(line);
        }
        if (area.points.size() > 2) {
            std::pair<util::Point, util::Point> line;
            line.first.x  = static_cast<int>(area.points[area.points.size() - 1].x * width);
            line.first.y  = static_cast<int>(area.points[area.points.size() - 1].y * height);
            line.second.x = static_cast<int>(area.points[0].x * width);
            line.second.y = static_cast<int>(area.points[0].y * height);
            lines.push_back(line);
        }
    }

    if (!area.linePoints.empty()) {
        auto pointTrans = [width, height](const MsgPoint& origin) {
            return util::Point{static_cast<int>(origin.x * width), static_cast<int>(origin.y * height)};
        };

        size_t drawDirectionIndex = area.linePoints.size() / 2;
        for (size_t i = 1; i < area.linePoints.size(); ++i) {
            auto lastPoint = pointTrans(area.linePoints[i - 1]);
            auto currPoint = pointTrans(area.linePoints[i]);

            auto lineDir    = currPoint - lastPoint;
            auto lineLength = util::Length(lineDir);
            if (lineLength == 0) {
                continue;
            }

            std::pair<util::Point, util::Point> line;
            line.first  = lastPoint;
            line.second = currPoint;
            lines.push_back(line);

            // Draw only one direction line
            if (drawDirectionIndex != i)
                continue;

            // Perpendicular line
            auto centPoint  = (lastPoint + currPoint) / 2;
            auto perpLength = height * 0.04;
            auto perpDir    = util::LineDirection(util::Perpendicular(lineDir), perpLength);
            auto finaPoint  = centPoint + perpDir;
            if (area.iderectionType == DirectionType::DirectionTypeOneWay) {
                // Vertical line
                lines.push_back(std::make_pair(centPoint, finaPoint));
            }

            // Arrow
            auto centPerpPoint = (centPoint + finaPoint) / 2;
            auto arrowDir      = util::LineDirection(lineDir, perpLength / 2);
            lines.push_back(std::make_pair(finaPoint, centPerpPoint + arrowDir));
            lines.push_back(std::make_pair(finaPoint, centPerpPoint + arrowDir * -1));

            // Two-way line   negative direction
            if (area.iderectionType == DirectionType::DirectionTypeTwoWay) {
                // Perpendicular line
                auto negativeLineDir = lastPoint - currPoint;
                auto negativePerpDir = util::LineDirection(util::Perpendicular(negativeLineDir), perpLength);
                auto negativeFinaPoint = centPoint + negativePerpDir;
                lines.push_back(
                    std::make_pair(finaPoint, negativeFinaPoint));  // Draw one less perpendicular line

                // Arrow
                auto negativeCentPerpPoint = (centPoint + negativeFinaPoint) / 2;
                auto negativeArrowDir      = util::LineDirection(negativeLineDir, perpLength / 2);
                lines.push_back(std::make_pair(negativeFinaPoint, negativeCentPerpPoint + negativeArrowDir));
                lines.push_back(
                    std::make_pair(negativeFinaPoint, negativeCentPerpPoint + negativeArrowDir * -1));
            }
        }
    }

    return lines;
}

std::vector<std::pair<util::Point, util::Point>> GetAreasOsdLines(const std::vector<MsgTaskArea>& areas,
                                                                  int width, int height) {
    std::vector<std::pair<util::Point, util::Point>> lines;
    for (auto& area : areas) {
        auto areaLines = GetAreaOsdLines(area, width, height);
        if (!areaLines.empty())
            lines.insert(lines.end(), areaLines.begin(), areaLines.end());
    }

    return lines;
}

void ShowAiDetectData(AlgChannelDataDetect& channelDet) {
    std::string algs = "";
    int index        = 0;
    for (auto alg : channelDet.atomicCodes) {
        if (0 == index)
            algs.append(alg);
        else
            algs.append(" ").append(alg);
        index++;
    }

    LOG_INFO("atomicCodesSize:{} [{}]", channelDet.atomicCodes.size(), algs);
}

}  // namespace cosmo