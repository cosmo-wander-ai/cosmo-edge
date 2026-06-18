// TaskAlarmSuppression.cc — Alarm suppression: compares with the previous alarm,
// suppresses alarms appearing at the same position. Configurable suppression
// duration and IOU threshold for same-position judgement.

#include "flow/alarm/TaskAlarmSuppression.h"

#include "util/Log.h"
#include "util/TimeUtil.h"

namespace cosmo {

TaskAlarmSuppression::TaskAlarmSuppression(const std::string& name) : m_name(name) {
    LOG_INFO("[{}]", m_name);
}

TaskAlarmSuppression::~TaskAlarmSuppression() {
    LOG_INFO("[{}]", m_name);
}

void TaskAlarmSuppression::ResetLastAlarmInfo() {
    m_lastAlarmRect = {0, 0, 0, 0};
    m_lastAlarmTime = 0;
    LOG_INFO("[{}] Reset History", m_name);
}

bool TaskAlarmSuppression::CheckReportAlarm(util::Box bbox, bool bSuppresion, float fIOUTh,
                                            uint32_t uiRestrainDurationThSec) {
    if (bSuppresion != m_bSuppresion) {
        m_bSuppresion = bSuppresion;
        LOG_INFO("[{}] AlarmSuppression {} fIOUTh:{} uiRestrainDurationThSec:{}", m_name,
                 bSuppresion ? "Open" : "Close", fIOUTh, uiRestrainDurationThSec);
        ResetLastAlarmInfo();
    }
    // Suppression not enabled — report alarm
    if (!bSuppresion) {
        return true;
    }

    // No suppression overlap area — do not suppress, report alarm
    if (fIOUTh <= 0.0f) {
        return true;
    }

    auto iouDiff = std::abs(fIOUTh - m_overlaprate);
    if ((iouDiff > 0.001f) || (uiRestrainDurationThSec != m_uiRestrainDurationSec)) {
        LOG_INFO("[{}] Param Changed IOU:{}->{} RestrainDurationSec:{}->{} ", m_name, m_overlaprate, fIOUTh,
                 m_uiRestrainDurationSec, uiRestrainDurationThSec);
        m_uiRestrainDurationSec = uiRestrainDurationThSec;
        m_overlaprate           = fIOUTh;
    }

    if (fIOUTh <= 0) {
        ResetLastAlarmInfo();
        LOG_INFO("[{}] IOU:{} [InvalidIou-ReportAlarm]", m_name, fIOUTh);
        return true;
    }

    auto alarmPts = util::GetMilliseconds();

    if (!m_lastAlarmRect.width && !m_lastAlarmRect.height) {
        m_lastAlarmRect = bbox;
        m_lastAlarmTime = alarmPts;
        LOG_INFO("[{}] bbox:{}.{} {}x{}, [FirstTarget-ReportAlarm]!", m_name, bbox.x, bbox.y, bbox.width,
                 bbox.height);
        return true;
    }
    bool bReportAlarm = false;
    // Compare intersection area
    auto square = IntersectionArea(m_lastAlarmRect, bbox);  // Intersection area

    auto product1 = bbox.height * bbox.width;
    auto product2 = m_lastAlarmRect.width * m_lastAlarmRect.height;
    auto product  = product1 + product2 - square;  // Union area

    if (product != 0) {
        float iou = static_cast<float>(square) / static_cast<float>(product);
        if (iou >= fIOUTh) {
            auto diff = alarmPts - m_lastAlarmTime;  // Time diff (ms)
            if (diff < 0)
                diff = 0;  // Guard against negative time difference.
            uint64_t diffSecs = static_cast<uint64_t>(diff) / 1000;
            uint64_t diffHour = diffSecs / 3600;
            uint64_t diffMim  = (diffSecs % 3600) / 60;
            uint64_t diffSec  = diffSecs % 60;

            if (diff >= static_cast<int64_t>(uiRestrainDurationThSec) * 1000) {
                bReportAlarm    = true;
                m_lastAlarmRect = bbox;
                m_lastAlarmTime = alarmPts;
                LOG_INFO(
                    "[{}] IOU:{} >= threshold:{}, Time:{}:{}:{} >= threashold:{}:{}:{}, "
                    "[SuppressionTimeout-ReportAlarm]!",
                    m_name, iou, fIOUTh, diffHour, diffMim, diffSec, uiRestrainDurationThSec / 3600,
                    (uiRestrainDurationThSec % 3600) / 60, uiRestrainDurationThSec % 60);
            } else {
                bReportAlarm = false;
                LOG_INFO(
                    "[{}] IOU:{} >= threshold:{}, Time:{}:{}:{} < threashold:{}:{}:{}, "
                    "[Suppression-DontReportAlarm]!",
                    m_name, iou, fIOUTh, diffHour, diffMim, diffSec, uiRestrainDurationThSec / 3600,
                    (uiRestrainDurationThSec % 3600) / 60, uiRestrainDurationThSec % 60);
            }
        } else {
            bReportAlarm    = true;
            m_lastAlarmRect = bbox;
            m_lastAlarmTime = alarmPts;

            LOG_INFO("[{}] IOU:{} < threshold:{} bbox:{}.{} {}x{} [NewTarget-ReportAlarm]!", m_name, iou,
                     fIOUTh, bbox.x, bbox.y, bbox.width, bbox.height);
        }
    } else {
        bReportAlarm    = true;
        m_lastAlarmRect = bbox;
        m_lastAlarmTime = alarmPts;

        LOG_ERRO(
            "[{}] Target IntersectionArea:{} UnionSet:{} lastAlarmRect:{}.{} {}x{}, bbox:{}.{} {}x{} Have No "
            "UnionSet  [NewTarget-ReportAlarm]!",
            m_name, square, product, m_lastAlarmRect.x, m_lastAlarmRect.y, m_lastAlarmRect.width,
            m_lastAlarmRect.height, bbox.x, bbox.y, bbox.width, bbox.height);
    }
    return bReportAlarm;
}

// Check if two boxes intersect
bool TaskAlarmSuppression::Intersect(util::Box alarmRect, util::Box other) {
    int left   = std::max(alarmRect.x, other.x);
    int top    = std::max(alarmRect.y, other.y);
    int right  = std::min(alarmRect.x + alarmRect.width, other.x + other.width);
    int bottom = std::min(alarmRect.y + alarmRect.height, other.y + other.height);

    return left < right && top < bottom;
}

// Compute intersection area
int TaskAlarmSuppression::IntersectionArea(util::Box alarmRect, util::Box other) {
    if (!Intersect(alarmRect, other)) {
        return 0;
    }

    int left   = std::max(alarmRect.x, other.x);
    int top    = std::max(alarmRect.y, other.y);
    int right  = std::min(alarmRect.x + alarmRect.width, other.x + other.width);
    int bottom = std::min(alarmRect.y + alarmRect.height, other.y + other.height);

    return (right - left) * (bottom - top);
}
}  // namespace cosmo