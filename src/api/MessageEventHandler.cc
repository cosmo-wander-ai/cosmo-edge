// MessageEventHandler — Message Event Handler implementation.

#include "api/MessageEventHandler.h"

#include "service/algorithm/IAlgorithmQuery.h"
#include "service/event/AlarmExport.h"
#include "service/event/IAlarmRecordService.h"
#include "service/network/INetworkConfig.h"
#include "util/DateTimeFormat.h"
#include "util/ErrorCode.h"
#include "util/TimeUtil.h"

namespace cosmo {

// ── Constructor ─────────────────────────────────────────────────────

MessageEventHandler::MessageEventHandler(service::IAlarmRecordService& alarm_service,
                                         service::IAlgorithmQuery& algorithm_query,
                                         service::INetworkConfig& network_config)
    : alarm_service_(alarm_service), algorithm_query_(algorithm_query), network_config_(network_config) {}

// ── Event Page Query ────────────────────────────────────────────────

Event::MsgPageSend MessageEventHandler::Handle(Event::MsgPageRecv&& data,
                                               std::error_condition& /*errc*/) const {
    Event::MsgPageSend retData{};
    retData.resData.rows = alarm_service_.QueryEvents(data, retData.resData.total);
    for (auto& event : retData.resData.rows) {
        event.algorithmName = algorithm_query_.GetAlgorithmName(event.algorithmCode);
    }
    return retData;
}

// ── Export Alarm ────────────────────────────────────────────────────

Event::MsgExportAlarmSend MessageEventHandler::Handle(Event::MsgExportAlarmRecv&& data,
                                                      std::error_condition& errc) const {
    // Determine export type from request category.
    ExportType export_type = ExportType::Behavior;
    if (!data.categorys.empty()) {
        export_type = CategoryToExportType(data.categorys[0]);
    }

    // Query all matching records (override pagination to export everything).
    int64_t total{0};
    data.pageNum  = 1;
    data.pageSize = 0;
    auto records  = alarm_service_.QueryEvents(data, total);

    // Delegate CSV generation to service layer.
    auto host_ip  = network_config_.GetHostIpAddress();
    auto file_url = ExportAlarmRecordsToCsv(records, export_type, host_ip, algorithm_query_, data.language);

    Event::MsgExportAlarmSend retData{};
    if (file_url.empty()) {
        errc = util::ErrorEnum::FileOpenFailed;
        return retData;
    }
    retData.resData.fileUrl = file_url;
    return retData;
}

// ── Passenger Flow Query — helpers ──────────────────────────────────

namespace {

    constexpr uint64_t HourToSecond(uint64_t value) {
        return value * 10000;
    }

    constexpr uint64_t HourToDaySecond(uint64_t value) {
        return value / 100 * 1000000;  // Clear hour portion
    }

    constexpr uint64_t HourToMonthSecond(uint64_t value) {
        return (value / 10000 * 100 + 1) * 1000000;  // Clear day portion
    }

    using TimePoint = Event::MsgQueryPassengerFlowNumberSend::TimePoint;

    void FillHourlyPoints(const std::vector<TimePoint>& src, uint64_t start_hour, uint64_t end_hour,
                          std::vector<TimePoint>& out) {
        size_t origin_idx = 0;
        auto hour_dt      = util::DateTimeFromYMDHMSInt(HourToSecond(start_hour));
        auto end_dt       = util::DateTimeFromYMDHMSInt(HourToSecond(end_hour));
        for (; hour_dt < end_dt; hour_dt = util::DateTime(hour_dt.ToTimeStamp() + 3600)) {
            TimePoint point{};
            if (origin_idx >= src.size()) {
                point.timeString = hour_dt.Time().ToHM();
            } else {
                auto origin_dt = util::DateTimeFromYMDHMSInt(HourToSecond(src[origin_idx].hour));
                if (hour_dt < origin_dt) {
                    point.timeString = hour_dt.Time().ToHM();
                } else {
                    point            = src[origin_idx++];
                    point.timeString = origin_dt.Time().ToHM();
                }
            }
            out.push_back(point);
        }
    }

    void FillDailyPoints(const std::vector<TimePoint>& src, uint64_t start_hour, uint64_t end_hour,
                         std::vector<TimePoint>& out) {
        size_t origin_idx = 0;
        auto day_dt       = util::DateTimeFromYMDHMSInt(HourToDaySecond(start_hour));
        auto end_dt       = util::DateTimeFromYMDHMSInt(HourToDaySecond(end_hour));
        for (; day_dt < end_dt; day_dt = util::DateTime(util::AddDay(day_dt.Date(), 1))) {
            TimePoint point{};
            if (origin_idx >= src.size()) {
                point.timeString = day_dt.Date().ToYMDZH();
            } else {
                auto origin_dt = util::DateTimeFromYMDHMSInt(HourToDaySecond(src[origin_idx].hour));
                if (day_dt < origin_dt) {
                    point.timeString = day_dt.Date().ToYMDZH();
                } else {
                    point            = src[origin_idx++];
                    point.timeString = origin_dt.Date().ToYMDZH();
                }
            }
            out.push_back(point);
        }
    }

    void FillWeeklyPoints(const std::vector<TimePoint>& src, uint64_t start_hour, uint64_t end_hour,
                          std::vector<TimePoint>& out) {
        size_t origin_idx = 0;
        auto day_dt       = util::DateTimeFromYMDHMSInt(HourToDaySecond(start_hour));
        auto end_dt       = util::DateTimeFromYMDHMSInt(HourToDaySecond(end_hour));

        int enter_number{0};
        int leave_number{0};

        for (; day_dt < end_dt; day_dt = util::DateTime(util::AddDay(day_dt.Date(), 1))) {
            if (origin_idx < src.size()) {
                auto origin_dt = util::DateTimeFromYMDHMSInt(HourToDaySecond(src[origin_idx].hour));
                if (!(day_dt < origin_dt)) {
                    const auto& point = src[origin_idx++];
                    enter_number += point.enterNumber;
                    leave_number += point.leaveNumber;
                }
            }

            if (day_dt.Date().WeekDay() == 0) {  // Sunday is the last day of the week
                TimePoint point{};
                point.enterNumber = enter_number;
                point.leaveNumber = leave_number;
                enter_number      = 0;
                leave_number      = 0;
                point.timeString  = util::GetYearWeek(day_dt.Date());
                out.push_back(point);
            }
        }
    }

    void FillMonthlyPoints(const std::vector<TimePoint>& src, uint64_t start_hour, uint64_t end_hour,
                           std::vector<TimePoint>& out) {
        size_t origin_idx = 0;
        auto mon_dt       = util::DateTimeFromYMDHMSInt(HourToMonthSecond(start_hour));
        auto end_dt       = util::DateTimeFromYMDHMSInt(HourToMonthSecond(end_hour));
        for (; mon_dt < end_dt; mon_dt = util::DateTime(util::AddMonth(mon_dt.Date(), 1))) {
            TimePoint point{};
            if (origin_idx >= src.size()) {
                point.timeString = mon_dt.Date().ToYMZH();
            } else {
                auto origin_dt = util::DateTimeFromYMDHMSInt(HourToMonthSecond(src[origin_idx].hour));
                if (mon_dt < origin_dt) {
                    point.timeString = mon_dt.Date().ToYMZH();
                } else {
                    point            = src[origin_idx++];
                    point.timeString = origin_dt.Date().ToYMZH();
                }
            }
            out.push_back(point);
        }
    }

}  // namespace

// ── Passenger Flow Query ────────────────────────────────────────────

Event::MsgQueryPassengerFlowNumberSend MessageEventHandler::Handle(
    Event::MsgQueryPassengerFlowNumberRecv&& data, std::error_condition& /*errc*/) const {
    Event::MsgQueryPassengerFlowNumberSend retData{};

    service::FlowQueryCondition condition;
    condition.cameraId       = data.channelId;
    condition.algorithm_code = data.algorithmCode;
    condition.type           = static_cast<service::FlowTimeGranularity>(data.type);
    condition.startHour      = util::DateTime(data.startTime).ToYMDHMSInt() / 10000;
    condition.endHour        = util::DateTime(data.endTime).ToYMDHMSInt() / 10000;
    condition.reported       = data.reported;
    data.startHour           = condition.startHour;
    data.endHour             = condition.endHour;

    auto sql_data = alarm_service_.QueryPassengerFlow(condition);

    // Convert service-layer DTOs to API response DTOs.
    std::vector<TimePoint> number_list;
    number_list.reserve(sql_data.numberList.size());
    for (const auto& unit : sql_data.numberList) {
        TimePoint web_unit;
        web_unit.channelId     = unit.cameraId;
        web_unit.algorithmCode = unit.algorithm_code;
        web_unit.enterNumber   = unit.enterNumber;
        web_unit.leaveNumber   = unit.leaveNumber;
        web_unit.hour          = unit.hour;
        number_list.push_back(web_unit);
    }

    // Fill gaps with zero-value time points based on the requested granularity.
    switch (static_cast<int>(data.type)) {
        case 1:
            FillHourlyPoints(number_list, data.startHour, data.endHour, retData.resData.numberList);
            break;
        case 2:
            FillDailyPoints(number_list, data.startHour, data.endHour, retData.resData.numberList);
            break;
        case 3:
            FillWeeklyPoints(number_list, data.startHour, data.endHour, retData.resData.numberList);
            break;
        case 4:
            FillMonthlyPoints(number_list, data.startHour, data.endHour, retData.resData.numberList);
            break;
        default:
            break;
    }

    retData.resData.totalCount = retData.resData.numberList.size();
    return retData;
}

}  // namespace cosmo
