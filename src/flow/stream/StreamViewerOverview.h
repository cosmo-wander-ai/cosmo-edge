#pragma once

#include <deque>
#include <list>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "media/Color.h"
#include "media/VideoFrame.h"
#include "util/DurationStat.h"
#include "util/MsgBaseTypes.h"
#include "util/Point.h"
#include "util/Thread.h"
#include "util/dto/AlgDataQueueTypes.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo {
// Overlay attribute priority: lower value = higher priority
enum class VideoOverviewAttrPriority {
    VideoOverviewAttrPriorityAlarmReport = 0,  // alarm         red/large font   display 3s or update
    VideoOverviewAttrPriorityAlarmFilter,      // alarm filtered blue/large font  display 3s or update
    VideoOverviewAttrPriorityArea,             // area           blue/2px line    permanent display
    VideoOverviewAttrPriorityBox,              // target box     yellow/1px line  display until next update
    VideoOverviewAttrPriorityTrackId,          // track ID       white/small font display until next update
    VideoOverviewAttrPriorityConfidence,       // confidence     white/small font display until next update
    VideoOverviewAttrPriorityBoxSize,          // box size       white/small font display until next update
    VideoOverviewAttrPriorityCount,            // target count   white/small font display until next update
    VideoOverviewAttrPriorityPassFlow,         // pass flow      white/small font display until next update

    VideoOverviewAttrPriorityOther  // other          white
};

struct StreamOverviewAttr {
    int fontSize{19};
    int lineWidth{2};
    int lineDiff{40};
    media::Color color{uint8_t(0), uint8_t(0), uint8_t(0)};
};

struct StreamOverviewTextEl {
    VideoOverviewAttrPriority attrPriority{VideoOverviewAttrPriority::VideoOverviewAttrPriorityOther};
    std::string text;
    media::Color bgColor{0, 0, 0};
    bool hasBgColor{false};
    cosmo::media::Color color{0, 0, 0};  // text color override (only effective when hasColor=true)
    bool hasColor{false};
};

struct StreamOverviewText {
    util::Point pos;  // position
    std::vector<StreamOverviewTextEl> posTexts;
};
struct StreamOverviewLine {
    VideoOverviewAttrPriority attrPriority{VideoOverviewAttrPriority::VideoOverviewAttrPriorityOther};
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
    // VideoOverviewAttrPriorityAlarmReport and VideoOverviewAttrPriorityAlarmFilter
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
    StreamViewerOverview(const std::string& channelId, const std::string& algId);
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
    void DrawActionDuration(VideoFramePtr frame);

    void AttrToColor(VideoOverviewAttrPriority attrPriority, StreamOverviewAttr& attr);

    void AddAlarmTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                             const StreamOverviewTextEl& text);
    void AddAlarmCountTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                  const StreamOverviewText& text);
    void AddAlarmPassFlowTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp,
                                     const StreamOverviewText& text);
    void AddTextToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp, util::Point pos,
                        StreamOverviewTextEl& text);
    void AddLineToLocal(int64_t streamIndex, uint64_t index, int64_t timestamp, StreamOverviewLine& line);
    void LiveDataHandTarget(int64_t streamIndex, uint64_t index, int64_t timestamp, MsgTarget& target);

    void ProcessPersonCountAlarm(const MsgRecAlarm& aiData);
    void ProcessPassFlowAlarm(const MsgRecAlarm& aiData);
    void ProcessOtherAlarm(const MsgRecAlarm& aiData);
    util::Point FindMinPoint(const std::vector<MsgPoint>& points, int width, int height);
    bool IsOverlappingTracked(const MsgTarget& target, const std::vector<MsgTarget>& targets);

    void LiveDataAiFrameToLocal(std::vector<MsgAiDetFrame>& aiData);
    void LiveDataSensitityToLocal(std::vector<MsgRecSensitity>& aiData);
    void LiveDataPosSaveSensitityToLocal(std::vector<MsgRecPosSaveSensitity>& aiData);
    void LiveDataAiFilterToLocal(std::vector<MsgAiFilterFrame>& aiData);
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

    int GetMaxTextLength(const std::vector<StreamOverviewTextEl>& posTexts);

    std::shared_mutex mtx_;
    std::string channel_id_;
    std::string alg_id_;  // algorithm ID
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

    // Action duration OSD
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
    static constexpr float kEmaAlpha =
        0.85f;  // responsive: Kalman already smooths, EMA just suppresses jitter
    static constexpr int64_t kSmoothExpireMs = 3000;  // expire after 3s unseen (frame-rate independent)
};
using StreamViewerOverviewPtr = std::shared_ptr<StreamViewerOverview>;
}  // namespace cosmo
