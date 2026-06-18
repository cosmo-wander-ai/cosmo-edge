// AiTrackerTypes.h — Private type definitions for AiTracker class.
// Shared across AiTracker.cc and AiTrackerArea.cc.
// NOT part of the public API — do not include from outside this directory.

#pragma once

#include <map>
#include <queue>
#include <string>

#include "flow/track/AiTracker.h"

struct cosmo::AiTracker::TrackIdLineArea {
    cosmo::TargetPositionOnLineType target_pos_now{cosmo::TargetPositionOnLineType::kUnknown};
    cosmo::AiDetectRstEl det_el;
    cosmo::TargetBreakLineType break_line_type{cosmo::TargetBreakLineType::kNone};
};

struct cosmo::AiTracker::TrackIdData {
    int track_id{-1};
    std::string track_id_info;
    bool detected{false};
    std::map<std::string, TrackIdLineArea> line_datas;
    cosmo::AiDetectRstEl det_el;
    std::queue<float> hw_ratios;
};
