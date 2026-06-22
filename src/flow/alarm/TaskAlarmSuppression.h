// TaskAlarmSuppression.h — Alarm suppression (dedup by position overlap).

#pragma once

#include <string>

#include "util/Rect.h"

namespace cosmo {

class TaskAlarmSuppression {
public:
    explicit TaskAlarmSuppression(const std::string& name);
    ~TaskAlarmSuppression();

public:
    // Currently only suppresses one box; when multiple boxes are sent, target has changed — reset.
    // bbox: detection box, fIOUTh: IOU threshold, uiRestrainDurationThSec: suppression duration (seconds)
    bool CheckReportAlarm(util::Box bbox, bool bSuppresion, float fIOUTh, uint32_t uiRestrainDurationThSec);

private:
    void ResetLastAlarmInfo();
    bool Intersect(util::Box alarmRect, util::Box other);        // Check intersection
    int IntersectionArea(util::Box alarmRect, util::Box other);  // Intersection area

private:
    std::string m_name;
    util::Box m_lastAlarmRect{0, 0, 0, 0};
    int64_t m_lastAlarmTime{0};                  // Timestamp (ms)
    bool m_bSuppresion{false};                   // Alarm suppression switch
    uint32_t m_uiRestrainDurationSec{6 * 3600};  // Suppression duration (seconds)
    float m_overlaprate{0.2f};                   // Overlap ratio
};

}  // namespace cosmo
