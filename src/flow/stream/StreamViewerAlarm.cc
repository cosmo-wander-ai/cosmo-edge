// StreamViewerAlarm.cc — Alarm overlay processing for StreamViewerOverview.
// Handles alarm text rendering: person count, pass flow, and generic alarms.
// Split from StreamViewerLiveData.cc to reduce file size (DEBT-007).

#include <algorithm>

#include "flow/stream/StreamViewerOverview.h"
#include "util/FormatString.h"

namespace cosmo {

void StreamViewerOverview::AddAlarmTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                               const StreamOverviewTextEl& text) {
    // data is expired
    if ((timestamp + 3000) < frame_identity_.timestamp) {
        return;
    }

    auto it = std::find_if(
        infos_.alarmOverviews.begin(), infos_.alarmOverviews.end(), [&](const auto& alarmOverview) {
            return (alarmOverview.streamIndex == streamIndex) && (alarmOverview.index == index);
        });
    if (it != infos_.alarmOverviews.end()) {
        it->text = text;
        return;
    }

    StreamOverviewAlarmInfo info;
    info.index       = index;
    info.streamIndex = streamIndex;
    info.timestamp   = timestamp;

    info.text = text;
    infos_.alarmOverviews.push_back(info);
}

void StreamViewerOverview::AddAlarmCountTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                                    const StreamOverviewText& text) {
    // data is expired
    if ((timestamp + 3000) < frame_identity_.timestamp) {
        return;
    }

    infos_.countOverviews.clear();
    StreamOverviewAlarmCountInfo info;
    info.index       = index;
    info.streamIndex = streamIndex;
    info.timestamp   = timestamp;
    info.text        = text;
    infos_.countOverviews.push_back(info);
}

void StreamViewerOverview::AddAlarmPassFlowTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                                       const StreamOverviewText& text) {
    // data is expired
    if ((timestamp + 10000) < frame_identity_.timestamp) {
        return;
    }

    infos_.passFlowOverviews.clear();
    StreamOverviewAlarmCountInfo info;
    info.index       = index;
    info.streamIndex = streamIndex;
    info.timestamp   = timestamp;
    info.text        = text;

    infos_.passFlowOverviews.push_back(info);
}

void StreamViewerOverview::ProcessPersonCountAlarm(const MsgRecAlarm& aiData) {
    for (const auto& area : params_.areas) {
        if (area.areaId == aiData.areaId) {
            StreamOverviewText text;
            text.pos = FindMinPoint(area.points, width_, height_);
            StreamOverviewTextEl posText;
            posText.text         = "COUNT: " + std::to_string(aiData.targetCount);
            posText.attrPriority = VideoOverviewAttrPriority::VideoOverviewAttrPriorityCount;
            text.posTexts.push_back(posText);
            AddAlarmCountTextToLocal(aiData.streamIndex, aiData.index, aiData.timestamp, text);
        }
    }
}

void StreamViewerOverview::ProcessPassFlowAlarm(const MsgRecAlarm& aiData) {
    StreamOverviewText text;
    text.pos = util::Point(width_ - 300, 300);
    StreamOverviewTextEl posText;
    int inAlarm  = 0;
    int outAlarm = 0;
    if (!pass_flow_info_.record && aiData.enterTotalCount > 0) {
        pass_flow_info_.enterNumber = aiData.enterTotalCount;
        pass_flow_info_.leaveNumber = aiData.leaveTotalCount;
        pass_flow_info_.record      = true;
    } else {
        inAlarm  = aiData.enterTotalCount - pass_flow_info_.enterNumber;
        outAlarm = aiData.leaveTotalCount - pass_flow_info_.leaveNumber;
    }

    posText.text         = "IN : " + std::to_string(inAlarm);
    posText.attrPriority = VideoOverviewAttrPriority::VideoOverviewAttrPriorityPassFlow;
    text.posTexts.push_back(posText);
    posText.text = "OUT: " + std::to_string(outAlarm);
    text.posTexts.push_back(posText);
    AddAlarmPassFlowTextToLocal(aiData.streamIndex, aiData.index, aiData.timestamp, text);
}

void StreamViewerOverview::ProcessOtherAlarm(const MsgRecAlarm& aiData) {
    StreamOverviewTextEl text;
    if (aiData.trackId >= 0) {
        text.text = "ALARM: ID " + std::to_string(aiData.trackId);
    } else {
        text.text = "ALARM";
    }

    if (!aiData.alarm) {
        text.attrPriority = VideoOverviewAttrPriority::VideoOverviewAttrPriorityAlarmFilter;
    } else {
        text.attrPriority = VideoOverviewAttrPriority::VideoOverviewAttrPriorityAlarmReport;
    }
    AddAlarmTextToLocal(aiData.streamIndex, aiData.index, aiData.timestamp, text);
}

util::Point StreamViewerOverview::FindMinPoint(const std::vector<MsgPoint>& points, int width, int height) {
    int y_min = height;
    int x_min = width;
    for (const auto& line : points) {
        int y = static_cast<int>(line.y * height);
        if (y < y_min) {
            y_min = y;
            x_min = static_cast<int>(line.x * width);
        }
    }
    return util::Point(x_min, y_min);
}

void StreamViewerOverview::LiveDataAlarmToLocal(std::vector<MsgRecAlarm>& aiDatas) {
    for (auto& aiData : aiDatas) {
        if (aiData.type == OnEventsPropertyType::PersonCount ||
            aiData.type == OnEventsPropertyType::CountNumber) {
            ProcessPersonCountAlarm(aiData);
        } else if (aiData.type == OnEventsPropertyType::People || aiData.type == OnEventsPropertyType::Car) {
            ProcessPassFlowAlarm(aiData);
        } else {
            ProcessOtherAlarm(aiData);
        }
    }
}

}  // namespace cosmo
