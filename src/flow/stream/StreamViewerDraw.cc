// StreamViewerDraw.cc — Drawing and rendering methods for StreamViewerOverview.
// Handles OSD overlay rendering: text/line drawing, attribute styling,
// pipeline duration display, and data retrieval from the local cache.

#include <algorithm>
#include <limits>
#include <set>

#include "flow/common/AreaLineUtil.h"
#include "flow/stream/StreamViewerOverview.h"
#include "flow/stream/StreamViewerOverviewTypes.h"
#include "media/Color.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameOSD.h"
#include "service/task/ITaskQuery.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

namespace cosmo {

static uint32_t PackLineKey(VideoOverviewAttrPriority prio, const media::Color& c) {
    return (static_cast<uint32_t>(prio) << 24) | (static_cast<uint32_t>(c.red) << 16) |
           (static_cast<uint32_t>(c.green) << 8) | c.blue;
}

StreamViewerOverview::OverviewInfo StreamViewerOverview::GetOverviewDataFromLocal(VideoFramePtr frame) {
    OverviewInfo info;

    const int64_t boxTimeWindowMs = std::max<int64_t>(frame_duration_, 3000);
    const uint64_t frameIdx       = frame->GetFrameIndex();
    const int64_t frameStreamIdx  = frame->GetStreamIndex();

    // ── Pass 1: exact frame index match, merge results from different pipeline stages ──
    bool exactFound = false;
    for (const auto& overviews : infos_.overviews) {
        if (overviews.streamIndex != frameStreamIdx) {
            continue;
        }
        if (overviews.index != frameIdx) {
            continue;
        }
        exactFound = true;
        // merge lines (do not clear, accumulate results from multiple stages)
        for (const auto& line : overviews.lines) {
            uint32_t key = PackLineKey(line.attrPriority, line.color);
            auto& group  = info.attrLines[key];
            group.color  = line.color;
            if (line.attrPriority == VideoOverviewAttrPriority::VideoOverviewAttrPriorityBox) {
                group.lineWidth = 3;  // corner bracket line thickened to 3px
            }
            group.lines.push_back(line.line);
        }
        // merge texts
        info.texts.insert(info.texts.end(), overviews.texts.begin(), overviews.texts.end());
    }

    // ── Pass 2: on exact match failure, fallback to nearest by time (with time window limit) ──
    if (!exactFound) {
        int64_t bestDuration = std::numeric_limits<int64_t>::max();
        for (const auto& overviews : infos_.overviews) {
            if (overviews.streamIndex != frameStreamIdx) {
                continue;
            }
            auto duration = std::abs(overviews.timestamp - frame->GetTimestamp());
            if (duration > boxTimeWindowMs) {
                continue;
            }
            if (duration >= bestDuration) {
                continue;
            }
            bestDuration = duration;
            info.attrLines.clear();
            for (const auto& line : overviews.lines) {
                uint32_t key = PackLineKey(line.attrPriority, line.color);
                auto& group  = info.attrLines[key];
                group.color  = line.color;
                if (line.attrPriority == VideoOverviewAttrPriority::VideoOverviewAttrPriorityBox) {
                    group.lineWidth = 3;  // corner bracket line thickened to 3px
                }
                group.lines.push_back(line.line);
            }
            info.texts = overviews.texts;
        }
    }

    for (const auto& countOverviews : infos_.countOverviews) {
        auto duration = std::abs(countOverviews.timestamp - frame->GetTimestamp());
        auto textCopy = countOverviews.text;
        if (duration > 3000) {
            if (!textCopy.posTexts.empty()) {
                textCopy.posTexts[0].text = "COUNT:  ";
            }
        }
        info.texts.push_back(textCopy);
        break;
    }

    for (const auto& passFlowOverviews : infos_.passFlowOverviews) {
        info.texts.push_back(passFlowOverviews.text);
        break;
    }

    bool bHaveAlarm = false;

    StreamOverviewTextEl alarmText;
    for (const auto& alarmOverviews : infos_.alarmOverviews) {
        if (!((alarmOverviews.streamIndex == frame->GetStreamIndex()) &&
              (alarmOverviews.timestamp + 3000 >= frame->GetTimestamp()))) {
            continue;
        }
        alarmText  = alarmOverviews.text;
        bHaveAlarm = true;
    }

    if (bHaveAlarm) {
        StreamOverviewText alarmOverviewText;
        alarmOverviewText.pos = util::Point(frame->GetWidth() - 300, 100);
        alarmOverviewText.posTexts.push_back(alarmText);
        info.texts.push_back(alarmOverviewText);
    }

    std::vector<std::pair<util::Point, util::Point>> areaLines;
    for (const auto& area : params_.areas) {
        auto arealine = GetAreaLines(area, frame->GetWidth(), frame->GetHeight());
        areaLines.insert(areaLines.end(), arealine.begin(), arealine.end());
    }

    if (!areaLines.empty()) {
        media::Color areaColor{0, 0, 0xff};
        uint32_t key = PackLineKey(VideoOverviewAttrPriority::VideoOverviewAttrPriorityArea, areaColor);
        info.attrLines[key].color = areaColor;
        info.attrLines[key].lines = areaLines;
    }

    return info;
}

void StreamViewerOverview::AttrToColor(VideoOverviewAttrPriority attrPriority, StreamOverviewAttr& attr) {
    if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityAlarmReport == attrPriority) {
        attr.color = {255, 107, 107};  // coral red #FF6B6B
    } else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityAlarmFilter == attrPriority) {
        attr.color = {99, 102, 241};  // electric blue #6366F1
    }

    else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityArea == attrPriority) {
        attr.color = {99, 102, 241};  // electric blue #6366F1
    } else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityBox == attrPriority) {
        attr.color = {34, 211, 238};  // cyan #22D3EE
    } else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityTrackId == attrPriority) {
        attr.color = {138, 148, 184};  // slate #8A94B8 — subtle ID
    } else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityConfidence == attrPriority) {
        attr.color = {220, 231, 255};  // cool white-blue #DCE7FF
    } else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityBoxSize == attrPriority) {
        attr.color = {138, 148, 184};  // slate #8A94B8
    } else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityCount == attrPriority) {
        attr.color = {220, 231, 255};  // cool white-blue #DCE7FF
    } else if (VideoOverviewAttrPriority::VideoOverviewAttrPriorityPassFlow == attrPriority) {
        attr.color    = {220, 231, 255};
        attr.fontSize = 19;
        attr.lineDiff = 70;
    } else {
        attr.color     = {220, 231, 255};
        attr.lineWidth = 2;
        attr.fontSize  = 19;
        attr.lineDiff  = 35;
    }
}

static size_t Utf8CharCount(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        if (c < 0x80)
            i += 1;
        else if ((c >> 5) == 0x6)
            i += 2;
        else if ((c >> 4) == 0xe)
            i += 3;
        else if ((c >> 3) == 0x1e)
            i += 4;
        else
            i += 1;  // invalid byte, skip
        count++;
    }
    return count;
}

VideoFramePtr StreamViewerOverview::FrameOverview(VideoFramePtr frame, OverviewInfo info) {
    for (auto& entry : info.attrLines) {
        auto& group = entry.second;
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().OSDDrawLines(
            group.lines, group.color, group.lineWidth);
    }

    size_t maxLine = 6;
    for (auto& text : info.texts) {
        size_t textsSize = text.posTexts.size();
        if (textsSize < 1) {
            continue;
        }

        int x      = text.pos.x;
        int y      = text.pos.y;
        auto lines = std::min(maxLine, textsSize);

        // compute actual total height: accumulate lineDiff for each row
        int totalHight = 0;
        for (size_t i = 0; i < lines; i++) {
            StreamOverviewAttr lineAttr;
            AttrToColor(text.posTexts[i].attrPriority, lineAttr);
            totalHight += lineAttr.lineDiff;
        }
        // enough space above
        if (y > totalHight) {
            // draw above
            y = y - totalHight;
        }

        auto maxTextLen = GetMaxTextLength(text.posTexts);

        if (x + maxTextLen >= width_) {
            // move to the left
            x = width_ - maxTextLen - 10;
        }

        if (x < 0 || y < 0) {
            continue;
        }

        for (size_t index = 0; index < lines; index++) {
            auto& textEl = text.posTexts[index];
            StreamOverviewAttr attr;
            AttrToColor(textEl.attrPriority, attr);

            // if text element has color override, use it
            if (textEl.hasColor) {
                attr.color = textEl.color;
            }

            if (textEl.hasBgColor) {
                // semi-transparent bg + outline: handled by OSDDrawTextEx in CPU blend pass
                service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().OSDDrawTextEx(
                    x, y, textEl.text, attr.color, attr.fontSize, textEl.bgColor, 199, true, 4);
            } else {
                // outline even without bg for better readability
                service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().OSDDrawTextEx(
                    x, y, textEl.text, attr.color, attr.fontSize, {0, 0, 0}, 0, true, 0);
            }
            y += attr.lineDiff;
        }
    }
    return frame;
}

void StreamViewerOverview::DrawActionDuration(VideoFramePtr frame) {
    if (!VideoFrameValid(frame) || task_id_.empty()) {
        return;
    }

    // refresh cache every 1 second
    auto now = cosmo::util::GetMilliseconds();
    if ((now - last_duration_update_ms_) > 1000) {
        last_duration_update_ms_ = now;
        cache_durations_ =
            service::ServiceRegistry::Instance().Get<service::ITaskQuery>().GetTaskActionDurations(task_id_,
                                                                                                   5000);
        cache_encode_duration_ = encode_duration_;
        cache_osd_duration_    = osd_duration_;
        osd_duration_          = {};  // reset accumulator

        // adaptively adjust OSD buffer window
        // only accumulate AI layer duration, skip business logic (alarm, sensitivity, etc.)
        static const std::set<std::string> kSkipActions = {
            "Alarm",     "FaceAlarm",   "AreaAlarm",      "BizFilter",          "Logic",
            "Branch",    "Sensitivity", "FixSensitivity", "PosSaveSensitivity", "Collect",
            "FaceLogic", "FriendDist",  "FilterLogic",    "AssoTarget",         "ChooseBest",
        };
        double aiPipelineMs = 0;
        for (auto& [name, dur] : cache_durations_) {
            if (dur.count > 0 && kSkipActions.find(name) == kSkipActions.end()) {
                aiPipelineMs += (dur.duration_ns / dur.count) / 1000000.0;
            }
        }
        // compute actual pipeline end-to-end latency: latest input frame ts - latest AI result ts in
        // overviews aiPipelineMs is only sum of per-frame processing, does not reflect queuing/serial actual
        // delay
        int64_t observedLatencyMs = 0;
        if (!infos_.overviews.empty() && fresh_frame_identity_.timestamp > 0) {
            int64_t latestAiTs = 0;
            for (const auto& ov : infos_.overviews) {
                latestAiTs = std::max(latestAiTs, ov.timestamp);
            }
            if (latestAiTs > 0 && fresh_frame_identity_.timestamp > latestAiTs) {
                observedLatencyMs = fresh_frame_identity_.timestamp - latestAiTs;
            }
        }
        int64_t estimatedMs = std::max(static_cast<int64_t>(aiPipelineMs * 1.5), observedLatencyMs);
        if (estimatedMs > 0) {
            frame_duration_ = std::clamp(estimatedMs, int64_t(50), int64_t(5000));
            frame_size_ = std::clamp(static_cast<size_t>(frame_duration_ / 40 + 5), size_t(10), size_t(200));
        }
    }

    if (cache_durations_.empty()) {
        return;
    }

    // draw duration info at top-left (per-line semi-transparent bg, auto-fit text width)
    media::Color debugText{94, 234, 212};  // mint teal #5EEAD4
    media::Color debugBg{3, 7, 18};        // near-black blue #030712
    int fontSize = 19;
    int x        = 50;
    int y        = 50;
    int lineStep = 36;

    service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().OSDDrawTextEx(
        x, y, "=== Pipeline Duration ===", debugText, fontSize, debugBg, 184, true, 5);
    y += lineStep;

    for (auto& [name, dur] : cache_durations_) {
        if (dur.count <= 0) {
            continue;
        }
        double avgMs = (dur.duration_ns / dur.count) / 1000000.0;
        auto text    = COSMO_FORMAT("{}: {:.2f}ms", name, avgMs);
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().OSDDrawTextEx(
            x, y, text, debugText, fontSize, debugBg, 184, true, 3);
        y += lineStep;
    }

    // OSD duration
    if (cache_osd_duration_.count > 0) {
        double osdMs = (cache_osd_duration_.duration_ns / cache_osd_duration_.count) / 1000000.0;
        auto text    = COSMO_FORMAT("OSD: {:.2f}ms", osdMs);
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().OSDDrawTextEx(
            x, y, text, debugText, fontSize, debugBg, 184, true, 3);
        y += lineStep;
    }

    // Encode duration
    if (cache_encode_duration_.count > 0) {
        double avgMs = (cache_encode_duration_.duration_ns / cache_encode_duration_.count) / 1000000.0;
        auto text    = COSMO_FORMAT("Encode: {:.2f}ms", avgMs);
        service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>().OSDDrawTextEx(
            x, y, text, debugText, fontSize, debugBg, 184, true, 3);
    }
}

void StreamViewerOverview::ShowText(StreamOverviewEl& overviewText) {
    for (auto& text : overviewText.texts) {
        LOG_INFO("Pos:{}.{}", text.pos.x, text.pos.y);
    }
}

void StreamViewerOverview::ShowOverviews(const std::string& tag) {
    for (auto& overviews : infos_.overviews) {
        ShowText(overviews);
        LOG_INFO("{} DEBUG:{}/{} Now Frame:{}/{}", tag, overviews.streamIndex, overviews.index,
                 frame_identity_.streamIndex, frame_identity_.index);
    }
}

int StreamViewerOverview::GetMaxTextLength(const std::vector<StreamOverviewTextEl>& posTexts) {
    int maxPixelWidth = 0;
    for (auto& posText : posTexts) {
        StreamOverviewAttr textAttr;
        AttrToColor(posText.attrPriority, textAttr);
        int charCount  = static_cast<int>(Utf8CharCount(posText.text));
        int pixelWidth = 0;
        if (textAttr.fontSize < 10) {
            pixelWidth = charCount * 11;
        } else {
            pixelWidth = textAttr.fontSize / 10 * 11 * charCount;
        }
        if (pixelWidth > maxPixelWidth) {
            maxPixelWidth = pixelWidth;
        }
    }
    return maxPixelWidth;
}

}  // namespace cosmo
