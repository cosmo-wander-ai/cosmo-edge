// SensitivityTypes.h — Private type definitions for Sensitivity class.
// Shared across Sensitivity.cc, SensitivityTrack.cc, SensitivityArea.cc.
// NOT part of the public API — do not include from outside this directory.
// IMPORTANT: This file must be included AFTER "flow/sensitivity/Sensitivity.h"
//            and OUTSIDE any namespace block.

#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "flow/sensitivity/Sensitivity.h"

struct cosmo::Sensitivity::TrackIdData {
    struct TrackIdDataEl {
        struct TrackIdAreaData {
            std::string id;  // Area ID
            std::string name;
        };
        bool bHaveArea{false};
        bool bHaveShieldedArea{false};
        std::vector<TrackIdAreaData> areas;
        std::vector<TrackIdAreaData> shildAreas;
        std::chrono::steady_clock::time_point timePoint;
        bool bLogicResult{false};
        bool bFilter{false};
        std::string filterDesc;
        cosmo::DataAlarmTargetConfidence targetCondidenceInfo;
    };

    unsigned trackId{0};
    std::string trackIdUuid;
    bool detected;
    bool historyFull{false};
    cosmo::AiDetectRstEl target;
    std::deque<TrackIdDataEl> history;
};

struct cosmo::Sensitivity::AreaIdDataEl {
    std::chrono::steady_clock::time_point timePoint;
    bool bResult{false};
    std::vector<cosmo::AiDetectRstEl> targets;
};

struct cosmo::Sensitivity::AreaTargetData {
    std::string id;
    std::string name;
    bool historyFull{false};
    std::deque<AreaIdDataEl> history;

    std::map<std::string, cosmo::Sensitivity::AreaTargetData> areas;
};

struct cosmo::Sensitivity::TargetSensitivityDetCount {
    std::string areaId;
    std::string areaName;
    bool inArea{false};
    size_t detectCount{0};
    size_t totalCount{0};
    size_t demCount{0};
    std::vector<bool> rsts;
};
