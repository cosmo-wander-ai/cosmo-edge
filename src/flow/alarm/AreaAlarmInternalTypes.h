// Internal-only definitions for AreaAlarm implementation units.
// IMPORTANT: This header is intentionally NOT included by AreaAlarm.h to keep
// public interface stable. It is only included by AreaAlarm*.cc files.

#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "flow/alarm/AreaAlarm.h"

namespace cosmo {

// --- Target-limit internal types ---

struct AreaAlarm::AreaTarget {
    //	steady_clock::time_point time_point;
    size_t stream_index;
    size_t frame_index;
    int64_t timestamp{0};
    std::vector<AiDetectRstEl> targets;
};

struct AreaAlarm::AreaTargetLimit {
    std::string area_id;
    std::string area_name;
    size_t target_count{0};
    bool history_full{false};
    std::deque<AreaAlarm::AreaTarget> history;
};

// --- Target-alarm internal types ---

struct AreaAlarm::TrackIdAreaDataUnit {
    std::chrono::steady_clock::time_point data_time_point;
    size_t frame_index{0};
    bool is_in_area{false};
    bool is_break_line{false};
};

struct AreaAlarm::TrackIdAreaData {
    bool has_into_area{false};
    bool has_out_area{false};
    std::deque<TrackIdAreaDataUnit> history;
};

struct AreaAlarm::TrackIdData {
    int track_id{-1};
    std::string track_id_uuid;
    bool detected;
    bool target_in_shielded_area{false};
    std::map<std::string, TrackIdAreaData> area_data;
    std::vector<std::string> pos_break_line_areas;  // Areas crossed in the forward direction
    std::vector<std::string> neg_break_line_areas;  // Areas crossed in the reverse direction
    AiDetectRstEl det_el;
    std::vector<DataAlarmTargetConfidence> det_els;
    std::chrono::steady_clock::time_point data_time_point;
};

// --- Pass-flow / wrong-direction internal types ---

struct AreaAlarm::PassFlowTrackIdData {
    unsigned track_id{0};
    bool detected{false};
    AiDetectRstEl target;
    std::deque<AiDetectRstEl> history;
};

struct AreaAlarm::PassFlowAreaTargets {
    std::string area_id;  // Primary area (line)
    std::string area_name;
    std::string associated_area;  // Associated area ID (bounding region)

    uint64_t hour{0};
    size_t enter_number{0};
    size_t leave_number{0};
    size_t enter_org_num{0};
    size_t leave_org_num{0};
    size_t enter_total_num{0};
    size_t leave_total_num{0};
    std::map<unsigned, AreaAlarm::PassFlowTrackIdData> target_map;
};

}  // namespace cosmo
