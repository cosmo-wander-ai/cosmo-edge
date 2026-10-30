// StreamViewerLiveData.cc — LiveData processing methods for StreamViewerOverview.
// Handles data collection: ingesting AI detection results and overlay data
// into the local cache (infos_) with deduplication and expiry logic.
// Alarm processing is in StreamViewerAlarm.cc.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "flow/stream/StreamViewerOverview.h"
#include "flow/stream/StreamViewerOverviewTypes.h"
#include "service/detail/ServiceRegistry.h"
#include "service/task/ITaskQuery.h"
#include "util/FormatString.h"
#include "util/GeometricPos.h"

namespace cosmo {

void StreamViewerOverview::AddTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                          util::Point pos, StreamOverviewTextEl& text,
                                          std::optional<util::Box> targetBox) {
    // data is expired (VOD strictly aligns frame sequence; live preview does not discard by frame seq,
    // otherwise inference frame number lags behind OSD frame, boxes never enter cache)
    if (!live_stream_) {
        if ((streamIndex < frame_identity_.streamIndex) || (index < frame_identity_.index)) {
            return;
        }
    } else {
        // Match decode frame: some detection paths do not write streamIndex (default 0),
        // while demuxer video track stream_idx is often 1, causing all boxes to be discarded.
        streamIndex = frame_identity_.streamIndex;
    }

    auto overviewIt = std::find_if(
        infos_.overviews.begin(), infos_.overviews.end(), [streamIndex, index](const auto& overview) {
            return (streamIndex == overview.streamIndex) && (index == overview.index);
        });

    if (overviewIt == infos_.overviews.end()) {
        // no match found - add new frame entry with position info
        StreamOverviewEl info;
        info.streamIndex = streamIndex;
        info.index       = index;
        info.timestamp   = timestamp;
        StreamOverviewText posText;
        posText.pos       = pos;
        posText.targetBox = targetBox;
        posText.posTexts.push_back(text);
        info.texts.push_back(posText);
        infos_.overviews.push_back(info);
        return;
    }

    auto& overview = *overviewIt;
    auto textIt =
        std::find_if(overview.texts.begin(), overview.texts.end(), [pos, &targetBox](const auto& localText) {
            const bool sameBox = !targetBox ? !localText.targetBox
                                            : localText.targetBox && localText.targetBox->x == targetBox->x &&
                                                  localText.targetBox->y == targetBox->y &&
                                                  localText.targetBox->width == targetBox->width &&
                                                  localText.targetBox->height == targetBox->height;
            return sameBox && (localText.pos.x == pos.x) && (localText.pos.y == pos.y);
        });

    if (textIt == overview.texts.end()) {
        // found frame but no matching position - add text with new position
        StreamOverviewText posText;
        posText.pos       = pos;
        posText.targetBox = targetBox;
        posText.posTexts.push_back(text);
        overview.texts.push_back(posText);
        return;
    }

    auto& localText = *textIt;
    for (auto& posTextValue : localText.posTexts) {
        if (text.targetLabel || posTextValue.targetLabel) {
            // Raw model labels are not prefixes of a formatted confidence string.
            // Keep separate results even when one label starts with another.
            if (text.targetLabel && posTextValue.targetLabel &&
                text.targetLabel->label == posTextValue.targetLabel->label &&
                text.targetLabel->score == posTextValue.targetLabel->score &&
                text.targetLabel->trackId == posTextValue.targetLabel->trackId) {
                return;
            }
            continue;
        }
        if (posTextValue.text == text.text) {
            // same position found same subtitle - update priority
            if (posTextValue.attrPriority > text.attrPriority) {
                posTextValue.attrPriority = text.attrPriority;
            }
            return;
        }
        // new text contains existing (e.g. "person:0.96 #23" contains "person:0.96") ->
        // replace
        if (text.text.find(posTextValue.text) == 0) {
            posTextValue = text;
            return;
        }
        // existing text contains new text -> skip
        if (posTextValue.text.find(text.text) == 0) {
            return;
        }
    }

    // same position no matching subtitle - add to position
    localText.posTexts.push_back(text);
}

void StreamViewerOverview::AddLineToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                          StreamOverviewLine& line) {
    if (!live_stream_) {
        if ((streamIndex < frame_identity_.streamIndex) || (index < frame_identity_.index)) {
            return;
        }
    } else {
        streamIndex = frame_identity_.streamIndex;
    }
    auto it = std::find_if(infos_.overviews.begin(), infos_.overviews.end(),
                           [streamIndex, index](const auto& overview) {
                               return (streamIndex == overview.streamIndex) && (index == overview.index);
                           });
    if (it == infos_.overviews.end()) {
        // no match found - add new frame
        StreamOverviewEl info;
        info.streamIndex = streamIndex;
        info.index       = index;
        info.timestamp   = timestamp;
        info.lines.push_back(line);
        infos_.overviews.push_back(info);
        return;
    }

    auto& overview = *it;
    auto lineIt = std::find_if(overview.lines.begin(), overview.lines.end(), [&line](const auto& localLine) {
        return (localLine.line.first.x == line.line.first.x) &&
               (localLine.line.first.y == line.line.first.y) &&
               (localLine.line.second.x == line.line.second.x) &&
               (localLine.line.second.y == line.line.second.y);
    });
    if (lineIt != overview.lines.end()) {
        if (lineIt->attrPriority > line.attrPriority) {
            lineIt->attrPriority = line.attrPriority;
        }
        return;
    }

    overview.lines.push_back(line);
}

// Alarm processing methods — moved to StreamViewerAlarm.cc

void StreamViewerOverview::LiveDataHandTarget(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                              MsgTarget& target) {
    // skip invalid targets in tracker LOSS state (confidence = -1 is tracker internal sentinel)
    bool hasValidConfidence = std::any_of(target.confidence.begin(), target.confidence.end(),
                                          [](const auto& conf) { return conf.confidence >= 0; });
    if (!hasValidConfidence) {
        return;
    }

    // A target's label or confidence is not an alarm state. Keep ordinary
    // detections a consistent cyan rather than hashing classes into warning colors.
    const media::Color boxColor{91, 205, 214};

    // ── update EMA smooth state (record only, does not change stored coordinates) ──
    if (target.trackId >= 0) {
        float bx = static_cast<float>(target.aiBox.x);
        float by = static_cast<float>(target.aiBox.y);
        float bw = static_cast<float>(target.aiBox.width);
        float bh = static_cast<float>(target.aiBox.height);
        auto it  = smoothed_boxes_.find(target.trackId);
        if (it != smoothed_boxes_.end()) {
            auto& sb             = it->second;
            sb.x                 = kEmaAlpha * bx + (1.0f - kEmaAlpha) * sb.x;
            sb.y                 = kEmaAlpha * by + (1.0f - kEmaAlpha) * sb.y;
            sb.w                 = kEmaAlpha * bw + (1.0f - kEmaAlpha) * sb.w;
            sb.h                 = kEmaAlpha * bh + (1.0f - kEmaAlpha) * sb.h;
            sb.lastSeenTimestamp = timestamp;
        } else {
            smoothed_boxes_[target.trackId] = {bx, by, bw, bh, timestamp};
        }
    }

    // ── Determine drawing coordinates: use EMA-smoothed if tracked, else raw ──
    int drawX = target.aiBox.x;
    int drawY = target.aiBox.y;
    int drawW = target.aiBox.width;
    int drawH = target.aiBox.height;

    if (target.trackId >= 0) {
        auto sit = smoothed_boxes_.find(target.trackId);
        if (sit != smoothed_boxes_.end()) {
            drawX = static_cast<int>(sit->second.x + 0.5f);
            drawY = static_cast<int>(sit->second.y + 0.5f);
            drawW = static_cast<int>(sit->second.w + 0.5f);
            drawH = static_cast<int>(sit->second.h + 0.5f);
        }
    }

    util::Point pointTL(drawX, drawY);
    util::Point pointTR(drawX + drawW, drawY);
    util::Point pointBR(drawX + drawW, drawY + drawH);
    util::Point pointBL(drawX, drawY + drawH);
    VideoOverviewAttrPriority attrPriority = VideoOverviewAttrPriority::kBox;

    int minSide = std::min(drawW, drawH);
    if (target.oriented_corners) {
        for (const auto& edge : GetQuadOsdLines(*target.oriented_corners, width_, height_)) {
            StreamOverviewLine line{attrPriority, edge, boxColor};
            AddLineToLocal(streamIndex, index, timestamp, line);
        }
    } else if (minSide < 30) {
        // small targets keep full rectangle, avoid cramped corner brackets
        StreamOverviewLine line1{attrPriority, {pointTL, pointTR}, boxColor};
        StreamOverviewLine line2{attrPriority, {pointTR, pointBR}, boxColor};
        StreamOverviewLine line3{attrPriority, {pointBR, pointBL}, boxColor};
        StreamOverviewLine line4{attrPriority, {pointBL, pointTL}, boxColor};
        AddLineToLocal(streamIndex, index, timestamp, line1);
        AddLineToLocal(streamIndex, index, timestamp, line2);
        AddLineToLocal(streamIndex, index, timestamp, line3);
        AddLineToLocal(streamIndex, index, timestamp, line4);
    } else {
        // corner bracket detection box — only draw L-shaped marks at 4 corners, cleaner look
        const int cornerLen = std::clamp(minSide / 6, 6, 18);
        // The integer-coordinate line interface approximates a 3px quarter arc
        // through its rounded 45-degree point (1,1). Two distinct segments keep
        // the fine rasterizer's round caps without repeated or zero-length edges.
        const int radius = 3;
        auto corner      = [&](util::Point p, int dx, int dy) {
            StreamOverviewLine horizontal{
                attrPriority, {{p.x + dx * radius, p.y}, {p.x + dx * cornerLen, p.y}}, boxColor};
            StreamOverviewLine bendStart{
                attrPriority, {{p.x + dx * radius, p.y}, {p.x + dx, p.y + dy}}, boxColor};
            StreamOverviewLine bendEnd{
                attrPriority, {{p.x + dx, p.y + dy}, {p.x, p.y + dy * radius}}, boxColor};
            StreamOverviewLine vertical{
                attrPriority, {{p.x, p.y + dy * radius}, {p.x, p.y + dy * cornerLen}}, boxColor};
            AddLineToLocal(streamIndex, index, timestamp, horizontal);
            AddLineToLocal(streamIndex, index, timestamp, bendStart);
            AddLineToLocal(streamIndex, index, timestamp, bendEnd);
            AddLineToLocal(streamIndex, index, timestamp, vertical);
        };
        corner(pointTL, 1, 1);
        corner(pointTR, -1, 1);
        corner(pointBR, -1, -1);
        corner(pointBL, 1, -1);
    }

    bool trackIdShown = false;
    bool sizeShown    = false;
    const util::Box displayBox{drawX, drawY, drawW, drawH};
    for (auto& confidence : target.confidence) {
        // skip invalid confidence entries
        if (confidence.confidence < 0) {
            continue;
        }
        util::Point pos(drawX, drawY);  // label position follows smoothed box
        StreamOverviewTextEl text;
        text.attrPriority = VideoOverviewAttrPriority::kConfidence;
        text.targetLabel  = StreamOverviewTextEl::TargetLabel{confidence.label, std::nullopt, -1};

        // Keep label and track ID without suggesting confidence is alarm severity.
        text.hasColor = true;
        text.color    = {235, 240, 249};

        text.text = confidence.label;
        if (confidence.scoreMeasured && std::isfinite(confidence.confidence) &&
            confidence.confidence >= 0.0F && confidence.confidence <= 1.0F) {
            text.text               = COSMO_FORMAT("{} {:.1f}%", text.text, confidence.confidence * 100.0F);
            text.targetLabel->score = confidence.confidence;
        }
        if (!trackIdShown && target.trackId >= 0) {
            text.text                 = COSMO_FORMAT("{} #{}", text.text, target.trackId);
            text.targetLabel->trackId = target.trackId;
            trackIdShown              = true;
        }
        AddTextToLocal(streamIndex, index, timestamp, pos, text, displayBox);

        // Width and height are the actual source detection box, not EMA-smoothed
        // display dimensions. For oriented geometry aiBox is its axis-aligned
        // enclosing rectangle; label that explicitly rather than suggesting the
        // dimensions belong to the rotated edges.
        if (!sizeShown && target.aiBox.width > 0 && target.aiBox.height > 0) {
            StreamOverviewTextEl size;
            size.attrPriority = VideoOverviewAttrPriority::kBoxSize;
            size.color        = {160, 174, 195};
            size.hasColor     = true;
            size.text         = target.oriented_corners
                                    ? COSMO_FORMAT("AABB {}x{} px", target.aiBox.width, target.aiBox.height)
                                    : COSMO_FORMAT("{}x{} px", target.aiBox.width, target.aiBox.height);
            AddTextToLocal(streamIndex, index, timestamp, pos, size, displayBox);
            sizeShown = true;
        }
    }
}

void StreamViewerOverview::LiveDataAiFrameToLocal(std::vector<MsgAiDetFrame>& aiDatas) {
    for (auto& aiData : aiDatas) {
        for (auto& target : aiData.targets) {
            LiveDataHandTarget(aiData.streamIndex, aiData.index, aiData.timestamp, target);
        }
    }
}

void StreamViewerOverview::LiveDataToLocal() {
    auto liveDatas =
        service::ServiceRegistry::Instance().Get<service::ITaskQuery>().GetTaskLiveOverviewInfo(task_id_);

    // ── Collect all AIData frames from every pipeline action, then deduplicate ──
    // Multiple actions (Track, Classify, Logic, ...) each record the same target;
    // we merge by (streamIndex, index) and per-target by trackId, keeping the
    // version with the richest confidence list (most downstream stage).
    std::map<std::pair<int64_t, int64_t>, MsgAiDetFrame> mergedFrames;

    for (auto& livedata : liveDatas) {
        if (MsgOverviewMemDataType::MsgOverviewMemDataTypeAIData == livedata.type) {
            for (auto& aiFrame : livedata.aiFrames) {
                auto key = std::make_pair(aiFrame.streamIndex, aiFrame.index);
                auto it  = mergedFrames.find(key);
                if (it == mergedFrames.end()) {
                    mergedFrames[key] = aiFrame;
                } else {
                    // Same frame from a different action: merge targets by trackId
                    for (auto& newTarget : aiFrame.targets) {
                        bool merged = false;
                        if (newTarget.trackId >= 0) {
                            auto targetIt = std::find_if(it->second.targets.begin(), it->second.targets.end(),
                                                         [&](const auto& existTarget) {
                                                             return existTarget.trackId == newTarget.trackId;
                                                         });
                            if (targetIt != it->second.targets.end()) {
                                if (newTarget.confidence.size() > targetIt->confidence.size()) {
                                    targetIt->confidence = newTarget.confidence;
                                    targetIt->attrs      = newTarget.attrs;
                                }
                                merged = true;
                            }
                        }
                        if (!merged) {
                            it->second.targets.push_back(newTarget);
                        }
                    }
                }
            }
        } else if (MsgOverviewMemDataType::MsgOverviewMemDataTypeSensitity == livedata.type ||
                   MsgOverviewMemDataType::MsgOverviewMemDataTypePosSaveSensitity == livedata.type ||
                   MsgOverviewMemDataType::MsgOverviewMemDataTypeAiFilter == livedata.type) {
            // Sensitivity and filter records currently do not produce live OSD elements.
            continue;
        } else if (MsgOverviewMemDataType::MsgOverviewMemDataTypeAlarm == livedata.type) {
            LiveDataAlarmToLocal(livedata.alarms);
        } else if (MsgOverviewMemDataType::MsgOverviewMemDataTypeParams == livedata.type) {
            params_.areas = livedata.params.areas;
        }
    }

    // ── Process deduplicated AI frames ──
    if (!mergedFrames.empty()) {
        std::vector<MsgAiDetFrame> deduped;
        deduped.reserve(mergedFrames.size());
        for (auto& [k, frame] : mergedFrames) {
            // Remove untracked targets (trackId<0, from Detect action) that overlap
            // with tracked targets (trackId>=0, from Track/Classify). The tracked version
            // is always more informative and already includes the detection bbox.
            auto& tgts = frame.targets;
            tgts.erase(
                std::remove_if(tgts.begin(), tgts.end(),
                               [this, &tgts](const MsgTarget& t) { return IsOverlappingTracked(t, tgts); }),
                tgts.end());
            deduped.push_back(std::move(frame));
        }
        LiveDataAiFrameToLocal(deduped);
    }
}

bool StreamViewerOverview::IsOverlappingTracked(const MsgTarget& target,
                                                const std::vector<MsgTarget>& targets) {
    if (target.trackId >= 0) {
        return false;  // keep tracked
    }
    // check if any tracked target has a similar bbox
    for (const auto& tracked : targets) {
        if (tracked.trackId < 0) {
            continue;
        }
        int dx   = std::abs(target.aiBox.x - tracked.aiBox.x);
        int dy   = std::abs(target.aiBox.y - tracked.aiBox.y);
        int dw   = std::abs(target.aiBox.width - tracked.aiBox.width);
        int dh   = std::abs(target.aiBox.height - tracked.aiBox.height);
        int side = std::max(tracked.aiBox.width, tracked.aiBox.height);
        int tol  = std::max(side / 5, 10);  // 20% tolerance
        if (dx < tol && dy < tol && dw < tol && dh < tol) {
            return true;  // overlapping untracked → remove
        }
    }
    return false;
}

}  // namespace cosmo
