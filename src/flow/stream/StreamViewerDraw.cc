// StreamViewerDraw.cc — Drawing and rendering methods for StreamViewerOverview.
// Handles OSD overlay rendering: text/line drawing, attribute styling,
// pipeline duration statistics, and data retrieval from the local cache.

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>
#include <tuple>

#include "flow/common/AreaLineUtil.h"
#include "flow/stream/StreamViewerOverview.h"
#include "flow/stream/StreamViewerOverviewTypes.h"
#include "media/Color.h"
#include "media/IOsdTextRenderer.h"
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
            if (line.attrPriority == VideoOverviewAttrPriority::kBox) {
                group.lineWidth = 2;
                group.fine      = true;
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
                if (line.attrPriority == VideoOverviewAttrPriority::kBox) {
                    group.lineWidth = 2;
                    group.fine      = true;
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
        // Align with the media accent of the console while retaining the exact
        // configured area geometry. This is independent of the viewer's theme.
        media::Color areaColor{168, 164, 255};
        uint32_t key              = PackLineKey(VideoOverviewAttrPriority::kArea, areaColor);
        info.attrLines[key].color = areaColor;
        info.attrLines[key].lines = areaLines;
    }

    return info;
}

void StreamViewerOverview::AttrToColor(VideoOverviewAttrPriority attrPriority, StreamOverviewAttr& attr) {
    if (VideoOverviewAttrPriority::kAlarmReport == attrPriority) {
        attr.color = {255, 107, 107};  // coral red #FF6B6B
    } else if (VideoOverviewAttrPriority::kAlarmFilter == attrPriority) {
        attr.color = {99, 102, 241};  // electric blue #6366F1
    }

    else if (VideoOverviewAttrPriority::kArea == attrPriority) {
        attr.color = {168, 164, 255};  // lavender #A8A4FF
    } else if (VideoOverviewAttrPriority::kBox == attrPriority) {
        attr.color = {85, 204, 216};  // soft cyan #55CCD8
    } else if (VideoOverviewAttrPriority::kTrackId == attrPriority) {
        attr.color = {138, 148, 184};  // slate #8A94B8 — subtle ID
    } else if (VideoOverviewAttrPriority::kConfidence == attrPriority) {
        attr.color    = {220, 231, 255};  // cool white-blue #DCE7FF
        attr.fontSize = 13;
        attr.lineDiff = 23;
    } else if (VideoOverviewAttrPriority::kBoxSize == attrPriority) {
        attr.color    = {138, 148, 184};  // slate #8A94B8
        attr.fontSize = 12;
        attr.lineDiff = 22;
    } else if (VideoOverviewAttrPriority::kCount == attrPriority) {
        attr.color = {220, 231, 255};  // cool white-blue #DCE7FF
    } else if (VideoOverviewAttrPriority::kPassFlow == attrPriority) {
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

namespace {
    int64_t OverlapArea(const util::Box& candidate, const util::Box& obstacle) {
        if (obstacle.width <= 0 || obstacle.height <= 0)
            return 0;
        // A little breathing room also accounts for the text renderer's 1px outline.
        constexpr int64_t gap = 2;
        const int64_t left    = std::max<int64_t>(candidate.x, int64_t(obstacle.x) - gap);
        const int64_t top     = std::max<int64_t>(candidate.y, int64_t(obstacle.y) - gap);
        const int64_t right =
            std::min(int64_t(candidate.x) + candidate.width, int64_t(obstacle.x) + obstacle.width + gap);
        const int64_t bottom =
            std::min(int64_t(candidate.y) + candidate.height, int64_t(obstacle.y) + obstacle.height + gap);
        return std::max<int64_t>(0, right - left) * std::max<int64_t>(0, bottom - top);
    }

    util::Box PlaceTargetLabel(const util::Box& preferred, const util::Box& target, int width, int height,
                               const std::vector<util::Box>& targetBoxes,
                               const std::vector<util::Box>& occupiedLabels) {
        constexpr int margin = 4;
        using Score          = std::tuple<int64_t, int64_t, int64_t>;
        util::Box best       = preferred;
        Score bestScore{std::numeric_limits<int64_t>::max(), 0, 0};
        auto consider = [&](int64_t x, int64_t y) {
            util::Box candidate{
                static_cast<int>(std::clamp<int64_t>(x, margin, width - margin - preferred.width)),
                static_cast<int>(std::clamp<int64_t>(y, margin, height - margin - preferred.height)),
                preferred.width, preferred.height};
            int64_t overlap = 0;
            for (const auto& obstacle : occupiedLabels)
                overlap += 4 * OverlapArea(candidate, obstacle);
            for (const auto& obstacle : targetBoxes)
                overlap += OverlapArea(candidate, obstacle);
            const int64_t dx = std::max({int64_t(0), int64_t(target.x) - candidate.x - candidate.width,
                                         int64_t(candidate.x) - target.x - target.width});
            const int64_t dy = std::max({int64_t(0), int64_t(target.y) - candidate.y - candidate.height,
                                         int64_t(candidate.y) - target.y - target.height});
            const int64_t movement =
                std::abs(int64_t(candidate.x) - preferred.x) + std::abs(int64_t(candidate.y) - preferred.y);
            const Score score{overlap, dx + dy, movement};
            if (score < bestScore) {
                bestScore = score;
                best      = candidate;
            }
            return overlap == 0;
        };
        // Preserve the approved single-target composition exactly when it is clear.
        if (consider(preferred.x, preferred.y))
            return best;
        const int64_t left         = int64_t(target.x) - 6;
        const int64_t rightAligned = int64_t(target.x) + target.width - preferred.width + 6;
        const int64_t above        = int64_t(target.y) - preferred.height - 6;
        const int64_t below        = int64_t(target.y) + target.height + 6;
        const int64_t right        = int64_t(target.x) + target.width + 8;
        const int64_t farLeft      = int64_t(target.x) - preferred.width - 8;
        consider(left, above);
        consider(rightAligned, above);
        consider(left, below);
        consider(rightAligned, below);
        consider(right, target.y);
        consider(farLeft, target.y);
        consider(right, int64_t(target.y) + target.height - preferred.height);
        consider(farLeft, int64_t(target.y) + target.height - preferred.height);
        // Deterministic bounded search, only needed after a collision. Candidates
        // extend by whole label rows instead of scanning the image pixel by pixel.
        for (int step = 1; step <= 8; ++step) {
            const int64_t shiftY = int64_t(step) * (preferred.height + 8);
            consider(left, above - shiftY);
            consider(rightAligned, above - shiftY);
            consider(left, below + shiftY);
            consider(rightAligned, below + shiftY);
            const int64_t shiftX = int64_t(step) * (preferred.width + 8);
            consider(right + shiftX, target.y);
            consider(farLeft - shiftX, target.y);
        }
        // Whole-row steps can skip a usable gap between existing labels after a
        // font-size change. Also try nearby occupied edges, with a fixed budget.
        std::vector<int64_t> rowEdges;
        rowEdges.reserve(occupiedLabels.size() * 2);
        for (const auto& obstacle : occupiedLabels) {
            rowEdges.push_back(std::clamp<int64_t>(int64_t(obstacle.y) - preferred.height - 2, margin,
                                                   height - margin - preferred.height));
            rowEdges.push_back(std::clamp<int64_t>(int64_t(obstacle.y) + obstacle.height + 2, margin,
                                                   height - margin - preferred.height));
        }
        std::sort(rowEdges.begin(), rowEdges.end(), [&](int64_t a, int64_t b) {
            return std::make_tuple(std::abs(a - preferred.y), a) <
                   std::make_tuple(std::abs(b - preferred.y), b);
        });
        rowEdges.erase(std::unique(rowEdges.begin(), rowEdges.end()), rowEdges.end());
        for (size_t i = 0; i < std::min<size_t>(rowEdges.size(), 16); ++i) {
            consider(preferred.x, rowEdges[i]);
            consider(left, rowEdges[i]);
            consider(rightAligned, rowEdges[i]);
        }
        return best;  // In a full image, minimize overlap without deleting a result.
    }
}  // namespace

VideoFramePtr StreamViewerOverview::FrameOverview(VideoFramePtr frame, OverviewInfo info) {
    PrepareTargetLayoutState();
    // The Sophon backend accepts at most 128 line segments per call. In
    // particular, sharing a semantic color between targets must not truncate
    // all boxes after the first full batch.
    constexpr size_t kLinesPerBatch = 128;
    auto& osd                       = service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>();
    for (auto& entry : info.attrLines) {
        auto& group = entry.second;
        for (size_t offset = 0; offset < group.lines.size(); offset += kLinesPerBatch) {
            const auto end = std::min(offset + kLinesPerBatch, group.lines.size());
            std::vector<std::pair<util::Point, util::Point>> batch(group.lines.begin() + offset,
                                                                   group.lines.begin() + end);
            if (group.fine)
                osd.OSDDrawLinesFine(std::move(batch), group.color, group.lineWidth);
            else
                osd.OSDDrawLines(std::move(batch), group.color, group.lineWidth);
        }
    }

    std::vector<util::Box> targetBoxes;
    std::vector<util::Box> occupiedLabels;
    for (const auto& text : info.texts) {
        if (text.targetBox)
            targetBoxes.push_back(*text.targetBox);
    }
    // One additional row for the original source-frame bounding-box size.
    size_t maxLine = 7;
    for (auto& text : info.texts) {
        if (std::any_of(text.posTexts.begin(), text.posTexts.end(),
                        [](const auto& row) { return row.targetLabel.has_value(); })) {
            DrawTargetPanel(text, targetBoxes, occupiedLabels);
            continue;
        }
        size_t textsSize = text.posTexts.size();
        if (textsSize < 1) {
            continue;
        }

        int x      = text.pos.x;
        int y      = text.pos.y;
        auto lines = std::min(maxLine, textsSize);

        // Detection annotations use one shared panel instead of separately
        // sized line backgrounds. All operations stay in the current session,
        // so Sophon composes outlines, panel and text in one host transfer.
        if (text.posTexts[0].attrPriority == VideoOverviewAttrPriority::kConfidence && width_ > 12 &&
            height_ > 12) {
            int panelWidth  = 0;
            int panelHeight = 6;
            for (size_t i = 0; i < lines; ++i) {
                auto& row  = text.posTexts[i];
                row.text   = FitTextToWidth(row, std::max(0, width_ - 14));
                panelWidth = std::max(panelWidth, GetTextWidth(row));
                StreamOverviewAttr attr;
                AttrToColor(row.attrPriority, attr);
                panelHeight += attr.lineDiff;
            }
            panelWidth = std::min(width_, panelWidth + 10);
            if (panelWidth <= 10 || panelHeight > height_)
                continue;
            const int panelX = std::clamp(x, 0, width_ - panelWidth);
            const int panelY =
                std::clamp(y >= panelHeight ? y - panelHeight : y + 2, 0, height_ - panelHeight);
            osd.OSDDrawFilledRect({panelX, panelY, panelWidth, panelHeight}, {16, 22, 36}, 232);
            osd.OSDDrawFilledRect({panelX, panelY + 3, 2, std::max(0, panelHeight - 6)}, {34, 211, 238}, 255);
            int rowY = panelY + 3;
            for (size_t i = 0; i < lines; ++i) {
                auto& row = text.posTexts[i];
                StreamOverviewAttr attr;
                AttrToColor(row.attrPriority, attr);
                if (row.hasColor)
                    attr.color = row.color;
                osd.OSDDrawTextEx(panelX + 7, rowY, row.text, attr.color, attr.fontSize, {0, 0, 0}, 0, true,
                                  0);
                rowY += attr.lineDiff;
            }
            continue;
        }

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

        int maxTextLen = 0;
        for (size_t i = 0; i < lines; ++i) {
            text.posTexts[i].text = FitTextToWidth(text.posTexts[i], std::max(0, width_ - 20));
            maxTextLen            = std::max(maxTextLen, GetTextWidth(text.posTexts[i]));
        }

        if (x + maxTextLen >= width_) {
            // move to the left
            x = std::max(0, width_ - maxTextLen - 10);
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

void StreamViewerOverview::PrepareTargetLayoutState() {
    const auto now = frame_identity_.timestamp;
    if (target_panel_stream_ != frame_identity_.streamIndex || target_panel_frame_width_ != width_ ||
        target_panel_frame_height_ != height_ || now < target_panel_timestamp_) {
        target_panel_states_.clear();
    }
    target_panel_stream_       = frame_identity_.streamIndex;
    target_panel_frame_width_  = width_;
    target_panel_frame_height_ = height_;
    target_panel_timestamp_    = now;
    for (auto it = target_panel_states_.begin(); it != target_panel_states_.end();) {
        if (now - it->second.lastSeen > kSmoothExpireMs)
            it = target_panel_states_.erase(it);
        else
            ++it;
    }
}

util::Box StreamViewerOverview::StabilizeTargetPanel(int trackId, const util::Box& preferred,
                                                     const util::Box& target,
                                                     const std::vector<util::Box>& targetBoxes,
                                                     const std::vector<util::Box>& occupiedLabels) {
    auto panel = PlaceTargetLabel(preferred, target, width_, height_, targetBoxes, occupiedLabels);
    if (trackId < 0)
        return panel;
    const auto now = frame_identity_.timestamp;
    auto found     = target_panel_states_.find(trackId);
    if (found != target_panel_states_.end()) {
        auto& state = found->second;
        int dx      = state.offsetX;
        // A right-side label follows the right edge when the target width changes.
        if (dx >= state.targetWidth)
            dx += target.width - state.targetWidth;
        const util::Box previous{target.x + dx, target.y + state.offsetY, preferred.width, preferred.height};
        const bool valid = previous.x >= 4 && previous.y >= 4 &&
                           int64_t(previous.x) + previous.width <= width_ - 4 &&
                           int64_t(previous.y) + previous.height <= height_ - 4 &&
                           state.panelWidth == previous.width && state.panelHeight == previous.height;
        if (valid) {
            auto overlap = [&](const util::Box& box) {
                int64_t result = 0;
                for (const auto& obstacle : targetBoxes)
                    result += OverlapArea(box, obstacle);
                for (const auto& obstacle : occupiedLabels)
                    result += 4 * OverlapArea(box, obstacle);
                return result;
            };
            const auto previousOverlap = overlap(previous);
            if (previousOverlap == 0 || overlap(panel) >= previousOverlap) {
                panel = previous;
                state.blockedSince.reset();
            } else {
                if (!state.blockedSince)
                    state.blockedSince = now;
                if (now - *state.blockedSince < kPanelSwitchDelayMs)
                    panel = previous;
                else
                    state.blockedSince.reset();
            }
        } else {
            state.blockedSince.reset();  // Frame boundaries and resized panels take precedence.
        }
    } else {
        if (target_panel_states_.size() >= kMaxTargetPanelStates) {
            auto oldest = std::min_element(
                target_panel_states_.begin(), target_panel_states_.end(), [](const auto& a, const auto& b) {
                    return std::tie(a.second.lastSeen, a.first) < std::tie(b.second.lastSeen, b.first);
                });
            target_panel_states_.erase(oldest);
        }
        found = target_panel_states_.emplace(trackId, TargetPanelState{}).first;
    }
    auto& state       = found->second;
    state.offsetX     = panel.x - target.x;
    state.offsetY     = panel.y - target.y;
    state.targetWidth = target.width;
    state.panelWidth  = panel.width;
    state.panelHeight = panel.height;
    state.lastSeen    = now;
    return panel;
}

void StreamViewerOverview::DrawTargetPanel(const StreamOverviewText& text,
                                           const std::vector<util::Box>& targetBoxes,
                                           std::vector<util::Box>& occupiedLabels) {
    constexpr int kMargin = 4, kPadding = 8, kColumnGap = 14;
    // Keep small text readable in the encoded preview without changing the translucent surface.
    constexpr int kPrimaryFont = 17, kSecondaryFont = 14;
    constexpr int kPrimaryScoreFont = 15, kSecondaryScoreFont = 14;
    constexpr int kIdFont = 13, kPercentFont = 12, kSizeHeight = 13;
    constexpr int kPrimaryHeight = 29, kSecondaryHeight = 18, kSinglePrimaryHeight = 21, kIdHeight = 19;
    if (width_ < 34 || height_ < 40)
        return;
    auto& osd = service::ServiceRegistry::Instance().Get<service::IVideoFrameOSD>();
    std::vector<const StreamOverviewTextEl::TargetLabel*> sourceRows;
    const StreamOverviewTextEl* dimensions = nullptr;
    int trackId                            = -1;
    for (const auto& row : text.posTexts) {
        if (row.targetLabel) {
            sourceRows.push_back(&*row.targetLabel);
            if (row.targetLabel->trackId >= 0)
                trackId = row.targetLabel->trackId;
        } else if (row.attrPriority == VideoOverviewAttrPriority::kBoxSize) {
            dimensions = &row;
        }
    }
    if (sourceRows.empty())
        return;
    // Reserve a complete 100.0% column, independent of the current measured score.
    // Each digit has a fixed advance; decimal and percent remain separate small fields.
    const auto digitAdvance = [&](int font) {
        int advance = 0;
        for (char ch = '0'; ch <= '9'; ++ch)
            advance = std::max(advance, GetFineTextWidth(std::string(1, ch), font));
        return std::max(1, advance - 2);
    };
    const auto numberWidth = [&](const std::string& number, int font) {
        int width = 2;
        for (char ch : number)
            width += ch == '.' ? std::max(3, GetFineTextWidth(".", font) - 2) : digitAdvance(font);
        return width;
    };
    struct Row {
        std::string label, id, number;
        int font{kSecondaryFont}, scoreFont{kSecondaryScoreFont}, scoreWidth{0}, height{kSecondaryHeight},
            idWidth{0};
        bool stacked{false}, omittedScore{false};
    };
    std::vector<Row> rows;
    int contentWidth = 0, contentHeight = 0;
    // Preserve readable padding where possible; exceptionally narrow frames use a compact fallback.
    const int padding          = std::min(kPadding, std::max(2, (width_ - 26) / 2));
    const int maxContentWidth  = std::min(320, width_ - kMargin * 2) - padding * 2;
    const int maxContentHeight = height_ - kMargin * 2 - padding * 2;
    if (maxContentHeight < kSecondaryHeight)
        return;
    size_t visibleRows = 0;
    for (size_t i = 0; i < sourceRows.size(); ++i) {
        const auto& value = *sourceRows[i];
        Row row;
        row.label     = value.label;
        row.id        = value.trackId >= 0 ? COSMO_FORMAT("#{}", value.trackId) : "";
        row.font      = std::min(i == 0 ? kPrimaryFont : kSecondaryFont, maxContentWidth - 2);
        row.scoreFont = i == 0 ? kPrimaryScoreFont : kSecondaryScoreFont;
        row.height    = i == 0 ? (sourceRows.size() > 1 ? kPrimaryHeight : kSinglePrimaryHeight)
                               : (i + 1 == sourceRows.size() ? kSecondaryFont : kSecondaryHeight);
        if (value.score)
            row.number = COSMO_FORMAT("{:.1f}", *value.score * 100.0F);
        if (!row.number.empty()) {
            row.scoreWidth = numberWidth("100.0", row.scoreFont) + 3 + GetFineTextWidth("%", kPercentFont);
            const int markerWidth = GetFineTextWidth("…", row.font);
            row.stacked           = row.scoreWidth + kColumnGap + markerWidth > maxContentWidth;
            if (row.stacked) {
                row.height = std::max(row.font, kIdHeight) + row.scoreFont + 6;
                if (row.scoreWidth > maxContentWidth) {
                    row.omittedScore = true;
                    row.scoreFont    = std::min(kSecondaryFont, maxContentWidth - 2);
                    row.scoreWidth   = GetFineTextWidth("…", row.scoreFont);
                }
            }
        }
        const int scoreSpace  = row.number.empty() || row.stacked ? 0 : row.scoreWidth + kColumnGap;
        const int labelBudget = std::max(0, maxContentWidth - scoreSpace);
        row.idWidth           = row.id.empty() ? 0 : GetFineTextWidth(row.id, kIdFont) + 10;
        // Preserve the ID independently; a narrow layout never emits a partial numeric ID.
        if (row.idWidth > labelBudget) {
            row.id.clear();
            row.idWidth = 0;
        }
        int idSpace = row.idWidth ? row.idWidth + 10 : 0;
        row.label   = FitFineTextToWidth(row.label, row.font, std::max(0, labelBudget - idSpace));
        if (row.label.empty()) {
            idSpace = row.idWidth;
            if (row.id.empty())
                row.label = FitFineTextToWidth("…", row.font, labelBudget);
        }
        const int reservedOverflow = i + 1 < sourceRows.size() ? kSecondaryHeight : 0;
        if (contentHeight + row.height + reservedOverflow > maxContentHeight)
            break;
        contentWidth =
            std::max(contentWidth, std::max(GetFineTextWidth(row.label, row.font) + idSpace + scoreSpace,
                                            row.stacked ? row.scoreWidth : 0));
        contentHeight += row.height;
        rows.push_back(std::move(row));
        ++visibleRows;
    }
    if (visibleRows < sourceRows.size()) {
        Row row;
        row.label = FitFineTextToWidth(COSMO_FORMAT("+{}", sourceRows.size() - visibleRows), kSecondaryFont,
                                       maxContentWidth);
        row.font  = kSecondaryFont;
        if (row.label.empty()) {
            row.font  = std::min(kSecondaryFont, maxContentWidth - 2);
            row.label = FitFineTextToWidth("…", row.font, maxContentWidth);
        }
        contentWidth = std::max(contentWidth, GetFineTextWidth(row.label, row.font));
        contentHeight += row.height;
        rows.push_back(std::move(row));
    }
    if (rows.empty())
        return;
    const int panelWidth  = std::min(maxContentWidth, std::max(150, contentWidth)) + padding * 2;
    const int panelHeight = padding * 2 + contentHeight;
    const auto box        = text.targetBox.value_or(util::Box{text.pos.x, text.pos.y, 0, 0});
    // The approved composition puts the card alongside the face, with an unobstructed image above it.
    const util::Box preferred{std::clamp(box.x + box.width + 13, kMargin, width_ - kMargin - panelWidth),
                              std::clamp(box.y - 9, kMargin, height_ - kMargin - panelHeight), panelWidth,
                              panelHeight};
    const auto panel = StabilizeTargetPanel(trackId, preferred, box, targetBoxes, occupiedLabels);
    occupiedLabels.push_back(panel);
    osd.OSDDrawRoundedRect(panel, {52, 50, 71}, 145, 5);
    osd.OSDDrawRoundedRectOutline(panel, {180, 177, 210}, 59, 5);
    int rowY = panel.y + padding;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (!row.label.empty())
            osd.OSDDrawTextFine(panel.x + padding, rowY, row.label,
                                i == 0 ? media::Color{245, 246, 251} : media::Color{228, 230, 240}, row.font);
        if (!row.id.empty()) {
            const int idX =
                panel.x + padding + GetFineTextWidth(row.label, row.font) + (row.label.empty() ? 0 : 10);
            osd.OSDDrawRoundedRectOutline({idX, rowY - 1, row.idWidth, kIdHeight}, {179, 173, 208}, 64, 4);
            osd.OSDDrawTextFine(idX + 5, rowY + 2, row.id, {220, 212, 238}, kIdFont);
        }
        if (!row.number.empty()) {
            const int numberY = rowY + (row.stacked ? std::max(row.font, kIdHeight) + 2 : i == 0 ? 1 : 0);
            const int right   = panel.x + panel.width - padding;
            if (row.omittedScore) {
                osd.OSDDrawTextFine(right - row.scoreWidth, numberY, "…", {230, 233, 243}, row.scoreFont);
            } else {
                const int percentWidth = GetFineTextWidth("%", kPercentFont);
                const int numberRight  = right - percentWidth - 3;
                int cursor             = numberRight - numberWidth(row.number, row.scoreFont);
                const auto color       = i == 0 ? media::Color{240, 242, 250} : media::Color{230, 233, 243};
                for (char ch : row.number) {
                    const std::string glyph(1, ch);
                    const int glyphWidth = GetFineTextWidth(glyph, row.scoreFont);
                    const int advance = ch == '.' ? std::max(3, glyphWidth - 2) : digitAdvance(row.scoreFont);
                    osd.OSDDrawTextFine(cursor + (advance - glyphWidth) / 2, numberY, glyph, color,
                                        row.scoreFont);
                    cursor += advance;
                }
                osd.OSDDrawTextFine(right - percentWidth, numberY + row.scoreFont - kPercentFont, "%",
                                    {212, 215, 231}, kPercentFont);
            }
        }
        if (i == 0 && rows.size() > 1 && !row.stacked)
            osd.OSDDrawRoundedRect(
                {panel.x + padding, rowY + kPrimaryHeight - 5, panel.width - padding * 2, 1}, {184, 181, 212},
                33, 0);
        rowY += row.height;
    }
    if (dimensions) {
        auto caption = FitFineTextToWidth(dimensions->text, kSizeHeight, width_ - kMargin * 2);
        if (caption.empty())
            caption = "…";
        const int captionWidth = GetFineTextWidth(caption, kSizeHeight);
        const int sizeX        = std::clamp(box.x, kMargin, width_ - kMargin - captionWidth);
        const int sizeY = std::clamp(box.y + box.height + 6, kMargin, height_ - kMargin - kSizeHeight - 2);
        const auto captionBox = PlaceTargetLabel({sizeX, sizeY, captionWidth, kSizeHeight + 2}, box, width_,
                                                 height_, targetBoxes, occupiedLabels);
        occupiedLabels.push_back(captionBox);
        osd.OSDDrawTextFine(captionBox.x, captionBox.y, caption, {226, 230, 239}, kSizeHeight);
    }
}

int StreamViewerOverview::GetFineTextWidth(const std::string& text, int pixelHeight) {
    if (text.empty())
        return 0;
    const auto key   = COSMO_FORMAT("fine:{}:{}", pixelHeight, text);
    const auto found = text_width_cache_.find(key);
    if (found != text_width_cache_.end())
        return found->second;
    auto& registry = service::ServiceRegistry::Instance();
    if (registry.Has<media::IOsdTextRenderer>()) {
        auto& renderer = registry.Get<media::IOsdTextRenderer>();
        if (renderer.IsReady()) {
            const auto bitmap = renderer.RenderStringWithFineOutline(text, static_cast<float>(pixelHeight));
            if (bitmap.width > 0) {
                if (text_width_cache_.size() >= 256)
                    text_width_cache_.clear();
                text_width_cache_.emplace(key, bitmap.width);
                return bitmap.width;
            }
        }
    }
    return static_cast<int>(Utf8CharCount(text)) * pixelHeight + 2;
}

std::string StreamViewerOverview::FitFineTextToWidth(const std::string& text, int pixelHeight, int maxWidth) {
    if (text.empty() || maxWidth <= 0)
        return {};
    // Bound user-defined text before rasterization and cut only at UTF-8 boundaries.
    constexpr size_t kMaxBytes = 256;
    if (text.size() <= kMaxBytes && GetFineTextWidth(text, pixelHeight) <= maxWidth)
        return text;
    if (GetFineTextWidth("...", pixelHeight) > maxWidth)
        return {};
    size_t end = std::min(text.size(), kMaxBytes);
    while (end < text.size() && end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    const auto prefix = text.substr(0, end);
    std::vector<size_t> boundaries{0};
    for (size_t i = 1; i <= prefix.size(); ++i) {
        if (i == prefix.size() || (static_cast<unsigned char>(prefix[i]) & 0xc0) != 0x80)
            boundaries.push_back(i);
    }
    size_t low = 0, high = boundaries.size() - 1;
    while (low < high) {
        const size_t mid = low + (high - low + 1) / 2;
        if (GetFineTextWidth(prefix.substr(0, boundaries[mid]) + "...", pixelHeight) <= maxWidth)
            low = mid;
        else
            high = mid - 1;
    }
    return prefix.substr(0, boundaries[low]) + "...";
}

void StreamViewerOverview::UpdateActionDuration(VideoFramePtr frame) {
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

int StreamViewerOverview::GetTextWidth(const StreamOverviewTextEl& text) {
    if (text.text.empty())
        return 0;
    StreamOverviewAttr attr;
    AttrToColor(text.attrPriority, attr);
    auto& registry = service::ServiceRegistry::Instance();
    if (registry.Has<media::IOsdTextRenderer>()) {
        auto& renderer = registry.Get<media::IOsdTextRenderer>();
        if (renderer.IsReady()) {
            const auto key   = std::to_string(attr.fontSize) + (text.hasBgColor ? ":b:" : ":n:") + text.text;
            const auto found = text_width_cache_.find(key);
            if (found != text_width_cache_.end())
                return found->second;
#if defined(COSMO_MEDIA_USE_SOPHON_BACKEND)
            constexpr float kRenderScale = 1.5F;
#else
            constexpr float kRenderScale = 1.0F;
#endif
            const auto bitmap = renderer.RenderStringWithOutline(text.text, attr.fontSize * kRenderScale);
            if (bitmap.width > 0) {
                const int width                   = bitmap.width + (text.hasBgColor ? 8 : 0);
                constexpr size_t kMaxCachedWidths = 256;
                if (text_width_cache_.size() >= kMaxCachedWidths)
                    text_width_cache_.clear();
                text_width_cache_.emplace(key, width);
                return width;
            }
        }
    }
    // Keep the prior estimate when the renderer has not been initialized.
    const int charCount = static_cast<int>(Utf8CharCount(text.text));
    return attr.fontSize < 10 ? charCount * 11 : attr.fontSize / 10 * 11 * charCount;
}

std::string StreamViewerOverview::FitTextToWidth(const StreamOverviewTextEl& text, int maxWidth) {
    if (text.text.empty() || maxWidth <= 0)
        return {};
    // Bound rasterization of untrusted custom labels before consulting the
    // renderer; scores and IDs are retained separately below.
    constexpr size_t kMaxPrefixBytes = 256;
    if (text.text.size() <= kMaxPrefixBytes && GetTextWidth(text) <= maxWidth)
        return text.text;

    // Preserve measured score and track ID while shortening an oversized class
    // label. Work on UTF-8 codepoint boundaries and use binary search to bound
    // the number of width measurements for long custom labels.
    std::string prefix = text.text;
    std::string suffix;
    const auto idPos = prefix.rfind(" #");
    if (idPos != std::string::npos && prefix.size() - idPos <= 16) {
        suffix = prefix.substr(idPos);
        prefix.resize(idPos);
    }
    if (!prefix.empty() && prefix.back() == '%') {
        const auto scorePos = prefix.rfind(' ');
        if (scorePos != std::string::npos && prefix.size() - scorePos <= 16) {
            suffix = prefix.substr(scorePos) + suffix;
            prefix.resize(scorePos);
        }
    }
    if (prefix.size() > kMaxPrefixBytes) {
        size_t end = kMaxPrefixBytes;
        while (end > 0 && (static_cast<unsigned char>(prefix[end]) & 0xc0) == 0x80)
            --end;
        prefix.resize(end);
    }
    std::vector<size_t> ends{0};
    for (size_t i = 1; i <= prefix.size(); ++i) {
        if (i == prefix.size() || (static_cast<unsigned char>(prefix[i]) & 0xc0) != 0x80)
            ends.push_back(i);
    }
    auto fits = [&](const std::string& candidate) {
        auto copy = text;
        copy.text = candidate;
        return GetTextWidth(copy) <= maxWidth;
    };
    if (!fits("..." + suffix)) {
        const auto nonSpace  = suffix.find_first_not_of(' ');
        const auto shortened = nonSpace == std::string::npos ? std::string{} : suffix.substr(nonSpace);
        return fits(shortened) ? shortened : std::string{};
    }
    size_t low  = 0;
    size_t high = ends.size() - 1;
    while (low < high) {
        const size_t mid = low + (high - low + 1) / 2;
        if (fits(prefix.substr(0, ends[mid]) + "..." + suffix))
            low = mid;
        else
            high = mid - 1;
    }
    return prefix.substr(0, ends[low]) + "..." + suffix;
}

}  // namespace cosmo
