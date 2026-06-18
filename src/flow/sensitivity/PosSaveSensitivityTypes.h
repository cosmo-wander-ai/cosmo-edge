// PosSaveSensitivityTypes.h — Private type definitions for PosSaveSensitivity.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "flow/sensitivity/PosSaveSensitivity.h"
#include "util/dto/ServerMsgTypes.h"

struct cosmo::PosSaveSensitivity::TrackIdData {
    struct TrackIdDataEl {
        struct TrackIdAreaData {
            std::string id;  // Area ID
            std::string name;
        };
        bool has_area{false};
        bool has_shielded_area{false};
        std::vector<TrackIdAreaData> areas;
        std::vector<TrackIdAreaData> shield_areas;
        bool logic_result{false};
        bool is_filtered{false};
        std::string filter_desc;
        DataAlarmTargetConfidence target_confidence_info;
    };

    unsigned track_id{0};
    std::string track_id_uuid;
    bool is_track_exist{false};  // Aging flag, target still exists. Set true immediately for new targets,
                                 // false when disappearing. Ages when false.
    bool is_behavior_detected{false};      // Set to true when behavior is detected, then no alarm
    bool logic_count_can_be_alarm{false};  // Logic count needs to reach standard for alarm
    size_t logic_count{0};
    VideoFramePtr frame{nullptr};  // Original decoded data frame
    size_t first_timestamp{0};     // Timestamp when appeared
    size_t last_timestamp{0};      // Timestamp before disappearance
    bool is_history_full{false};   // Calculate sensitivity only after history queue has been popped
    AiDetectRstEl target;
    DataAlarmTargetConfidence target_confidence_info;
    std::vector<AiDetectRstEl> group_targets;
    std::deque<TrackIdDataEl> history;
};
