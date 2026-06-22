// System config data types — extracted from util/config/CfgAlarmParam.h,
// CfgRebootParam.h to break compile-time coupling between service interfaces
// and config singleton managers.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace cosmo {

struct CfgAlarmParamOverviewInfo {
    int picQuality{75};             // Panorama picture quality
    bool alarmTypeOverview{true};   // Alarm type overlay
    bool areaOverview{true};        // Alarm area overlay
    bool targetBoxOverview{true};   // Target box overlay
    bool targetSizeOverview{true};  // Target size overlay
    friend void to_json(nlohmann::json& j, const CfgAlarmParamOverviewInfo& v);
    friend void from_json(const nlohmann::json& j, CfgAlarmParamOverviewInfo& v);
};

struct CfgAlarmParamVideoRecordInfo {
    bool bopen{false};     // Alarm recording enabled
    int preDuration{5};    // Recording duration: 5 seconds before alarm
    int aftreDuration{5};  // Recording duration: 5 seconds after alarm
    friend void to_json(nlohmann::json& j, const CfgAlarmParamVideoRecordInfo& v);
    friend void from_json(const nlohmann::json& j, CfgAlarmParamVideoRecordInfo& v);
};

struct CfgRebootParamInfo {
    bool isTimingRestart{true};    // Whether to restart periodically
    int weekDay{0};                // Restart weekday
    int restartTimeSec{3600 * 2};  // Restart time: 2 AM
    friend void to_json(nlohmann::json& j, const CfgRebootParamInfo& v);
    friend void from_json(const nlohmann::json& j, CfgRebootParamInfo& v);
};

}  // namespace cosmo
