#pragma once

#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "media/Color.h"
#include "media/VideoFrame.h"
#include "util/DurationStat.h"
#include "util/MsgBaseTypes.h"
#include "util/Point.h"
#include "util/Rect.h"
#include "util/Thread.h"
#include "util/dto/AlgDataQueueTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo {
// Overlay attribute priority: lower value = higher priority
enum class VideoOverviewAttrPriority {
    kAlarmReport = 0,  // alarm         red/large font   display 3s or update
    kAlarmFilter,      // alarm filtered blue/large font  display 3s or update
    kArea,             // area           lavender line    permanent display
    kBox,              // target box     cyan line        display until next update
    kTrackId,          // track ID       white/small font display until next update
    kConfidence,       // confidence     white/small font display until next update
    kBoxSize,          // box size       white/small font display until next update
    kCount,            // target count   white/small font display until next update
    kPassFlow,         // pass flow      white/small font display until next update

    kOther  // other          white
};

struct StreamOverviewAttr {
    int fontSize{19};
    int lineWidth{2};
    int lineDiff{40};
    media::Color color{uint8_t(0), uint8_t(0), uint8_t(0)};
};

struct StreamOverviewTextEl {
    struct TargetLabel {
        std::string label;
        std::optional<float> score;
        int trackId{-1};
    };
    VideoOverviewAttrPriority attrPriority{VideoOverviewAttrPriority::kOther};
    std::string text;
    media::Color bgColor{0, 0, 0};
    bool hasBgColor{false};
    cosmo::media::Color color{0, 0, 0};  // text color override (only effective when hasColor=true)
    bool hasColor{false};
    // Keep values separate from their legacy text projection. A model label may
    // itself contain spaces, percentages or '#' and must never be parsed as UI.
    std::optional<TargetLabel> targetLabel;
};

struct StreamOverviewText {
    util::Point pos;  // position
    std::vector<StreamOverviewTextEl> posTexts;
    std::optional<util::Box> targetBox;  // display geometry; dimensions remain measured source values
};
struct StreamOverviewLine {
    VideoOverviewAttrPriority attrPriority{VideoOverviewAttrPriority::kOther};
    std::pair<util::Point, util::Point> line;
    media::Color color{200, 200, 200};  // default gray, can be overridden per label
};

struct StreamOverviewIdentity {
    int64_t streamIndex{0};  // stream ID
    uint64_t index{0};       // frame sequence
    int64_t timestamp{0};    // frame timestamp
};

struct StreamOverviewEl : public StreamOverviewIdentity {
    bool bNeedOld{false};
    std::vector<StreamOverviewText> texts;
    std::vector<StreamOverviewLine> lines;
};

struct StreamOverviewAlarmInfo : public StreamOverviewIdentity {
    StreamOverviewTextEl text;
};

struct StreamOverviewAlarmCountInfo : public StreamOverviewIdentity {
    StreamOverviewText text;
};

struct StreamOverviewInfo {
    std::vector<StreamOverviewEl> overviews;
    // kAlarmReport and kAlarmFilter
    std::vector<StreamOverviewAlarmInfo> alarmOverviews;
    std::vector<StreamOverviewAlarmCountInfo> countOverviews;
    std::vector<StreamOverviewAlarmCountInfo> passFlowOverviews;
};

struct StreamOverviewPassFlowRecord {
    bool record{false};
    int enterNumber{-1};
    int leaveNumber{-1};
};

class StreamViewerOverview {
public:
    StreamViewerOverview(const std::string& channelId, const std::string& algId,
                         uint64_t timedPreviewToken = 0);
    ~StreamViewerOverview();

    VideoFramePtr HandFrame(VideoFramePtr frame);
    MsgOverviewDebugInfo GetProcInfo() {
        return debug_info_;
    }
    void SetEncodeDuration(const util::DurationStatInfo& info) {
        encode_duration_ = info;
    }

private:
    class AttrLines;
    class OverviewInfo;
    void HandOverview(VideoFramePtr frame);
    void UpdateActionDuration(VideoFramePtr frame);

    void AttrToColor(VideoOverviewAttrPriority attrPriority, StreamOverviewAttr& attr);

    void AddAlarmTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                             const StreamOverviewTextEl& text);
    void AddAlarmCountTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                  const StreamOverviewText& text);
    void AddAlarmPassFlowTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                     const StreamOverviewText& text);
    void AddTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp, util::Point pos,
                        StreamOverviewTextEl& text, std::optional<util::Box> targetBox = std::nullopt);
    void AddLineToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp, StreamOverviewLine& line);
    void LiveDataHandTarget(int64_t streamIndex, uint64_t index, int64_t timestamp, MsgTarget& target);

    void ProcessPersonCountAlarm(const MsgRecAlarm& aiData);
    void ProcessPassFlowAlarm(const MsgRecAlarm& aiData);
    void ProcessOtherAlarm(const MsgRecAlarm& aiData);
    util::Point FindMinPoint(const std::vector<MsgPoint>& points, int width, int height);
    bool IsOverlappingTracked(const MsgTarget& target, const std::vector<MsgTarget>& targets);

    void LiveDataAiFrameToLocal(std::vector<MsgAiDetFrame>& aiData);
    void LiveDataAlarmToLocal(std::vector<MsgRecAlarm>& aiData);
    void OldLocalData();
    void LiveDataToLocal();
    void HandLiveOverview(VideoFramePtr frame);

    void HandVodOverview(VideoFramePtr frame);

    OverviewInfo GetOverviewDataFromLocal(VideoFramePtr frame);
    VideoFramePtr FrameOverview(VideoFramePtr frame, OverviewInfo info);

    VideoFramePtr FrameCache(VideoFramePtr frame);

    void ShowText(StreamOverviewEl& overviewText);
    void ShowOverviews(const std::string& tag);

    int GetTextWidth(const StreamOverviewTextEl& text);
    std::string FitTextToWidth(const StreamOverviewTextEl& text, int maxWidth);
    int GetFineTextWidth(const std::string& text, int pixelHeight);
    std::string FitFineTextToWidth(const std::string& text, int pixelHeight, int maxWidth);
    void DrawTargetPanel(const StreamOverviewText& text, const std::vector<util::Box>& targetBoxes,
                         std::vector<util::Box>& occupiedLabels);

    void PrepareTargetLayoutState();
    util::Box StabilizeTargetPanel(int trackId, const util::Box& preferred, const util::Box& target,
                                   const std::vector<util::Box>& targetBoxes,
                                   const std::vector<util::Box>& occupiedLabels);
    struct TargetPanelState {
        int offsetX{0}, offsetY{0};
        int targetWidth{0};
        int panelWidth{0}, panelHeight{0};
        int64_t lastSeen{0};
        std::optional<int64_t> blockedSince;
    };
    std::unordered_map<int, TargetPanelState> target_panel_states_;
    int64_t target_panel_stream_{0};
    int64_t target_panel_timestamp_{0};
    int target_panel_frame_width_{0}, target_panel_frame_height_{0};
    static constexpr size_t kMaxTargetPanelStates = 256;
    static constexpr int64_t kPanelSwitchDelayMs  = 200;

    std::shared_mutex mtx_;
    std::string channel_id_;
    std::string alg_id_;  // algorithm ID
    uint64_t timed_preview_token_{0};
    std::string task_id_;
    MsgOverviewDebugInfo debug_info_;
    // Overlay data retention window (FrameCache removed; affects GetOverviewDataFromLocal fallback and
    // OldLocalData cleanup)
    int64_t frame_duration_{50};  // ms
    size_t frame_size_{50};       // overlay cache entry limit (OldLocalData cleanup)
    bool live_stream_{false};
    uint64_t hand_data_count_{0};
    int width_{0};
    int height_{0};
    StreamOverviewPassFlowRecord pass_flow_info_;
    StreamOverviewIdentity frame_identity_;        // frame being processed
    StreamOverviewIdentity fresh_frame_identity_;  // latest frame
    StreamOverviewInfo infos_;
    MsgTaskConfig params_;
    std::deque<VideoFramePtr> frames_;

    // Duration statistics also drive the adaptive overlay matching window.
    int64_t last_duration_update_ms_{0};
    std::vector<std::pair<std::string, util::DurationStatInfo>> cache_durations_;
    util::DurationStatInfo encode_duration_;
    util::DurationStatInfo cache_encode_duration_;
    util::DurationStatInfo osd_duration_;
    util::DurationStatInfo cache_osd_duration_;

    // ── Box EMA smoothing ──
    struct SmoothedBox {
        float x{0}, y{0}, w{0}, h{0};
        int64_t lastSeenTimestamp{0};  // last updated timestamp (ms)
    };
    std::unordered_map<int, SmoothedBox> smoothed_boxes_;  // key: trackId
    // Avoid re-rasterizing the same text solely to measure its width every frame.
    // The cache is capped and cleared in GetTextWidth.
    std::unordered_map<std::string, int> text_width_cache_;
    static constexpr float kEmaAlpha =
        0.85f;  // responsive: Kalman already smooths, EMA just suppresses jitter
    static constexpr int64_t kSmoothExpireMs = 3000;  // expire after 3s unseen (frame-rate independent)
};
using StreamViewerOverviewPtr = std::shared_ptr<StreamViewerOverview>;
}  // namespace cosmo
