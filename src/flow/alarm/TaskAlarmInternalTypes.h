// Internal-only definitions for TaskAlarm implementation units.
// IMPORTANT: This header is intentionally NOT included by TaskAlarm.h to keep
// public interface stable. It is only included by TaskAlarm*.cc files.

#pragma once

#include <chrono>
#include <map>
#include <string>

namespace cosmo {

// Definitions of TaskAlarm's private nested types (forward-declared in TaskAlarm.h).
struct TaskAlarm::AlarmIdData {
    struct AlarmIdAreaData {
        int64_t inAreaTime{0};
        std::string inAreaFullImageUrl;
    };
    int trackId{0};
    bool sign{false};
    int alarmCount{0};
    std::map<std::string, AlarmIdAreaData> areasInfo;
    std::chrono::steady_clock::time_point lastAlarmTime;
};

struct TaskAlarm::AreaIdData {
    std::string areaId;
    bool haveReport{false};
    std::chrono::steady_clock::time_point lastAlarmTime;
};

}  // namespace cosmo
