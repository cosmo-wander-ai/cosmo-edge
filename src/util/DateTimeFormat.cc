// DateTimeFormat — Date Time Format implementation.

#include "util/DateTimeFormat.h"

#include <string>

#include "util/Exception.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace {

/// Converts a digit pair to two ASCII characters (e.g., 12 → '1','2').
constexpr char DigitHi(uint8_t n) {
    return static_cast<char>(n / 10 + '0');
}
constexpr char DigitLo(uint8_t n) {
    return static_cast<char>(n % 10 + '0');
}

}  // namespace

namespace cosmo::util {

// ---------------------------------------------------------------------------
// HMSTime
// ---------------------------------------------------------------------------

HMSTime::HMSTime() : hour_{0}, minute_{0}, second_{0} {}

HMSTime::HMSTime(std::string_view str) : HMSTime{} {
    if (!str.empty()) {
        if (str.size() != 5 && str.size() != 8) {
            throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
        }
        // Construct null-terminated string for sscanf safety.
        std::string safe(str);
        int h{0}, m{0}, s{0};
        int ret = sscanf(safe.c_str(), "%d:%d:%d", &h, &m, &s);
        if ((ret != 2 && ret != 3) || h >= 24 || m >= 60 || s >= 60) {
            throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
        }
        hour_   = static_cast<uint8_t>(h);
        minute_ = static_cast<uint8_t>(m);
        second_ = static_cast<uint8_t>(s);
    }
}

HMSTime::HMSTime(uint8_t h, uint8_t m, uint8_t s) : hour_(h), minute_(m), second_(s) {
    if (h >= 24 || m >= 60 || s >= 60) {
        throw Exception(COSMO_FORMAT("wrong format, [{:02}:{:02}:{:02}]", h, m, s));
    }
}

HMSTime::HMSTime(uint32_t sec) {
    constexpr uint32_t kMaxDaySec = 3600 * 24 - 1;
    if (sec > kMaxDaySec) {
        hour_   = 23;
        minute_ = 59;
        second_ = 59;
    } else {
        hour_   = static_cast<uint8_t>(sec / 3600 % 24);
        minute_ = static_cast<uint8_t>(sec / 60 % 60);
        second_ = static_cast<uint8_t>(sec % 60);
    }
}

HMSTime::HMSTime(std::time_t timestamp) : HMSTime{} {
    tm local_tm;
    localtime_r(&timestamp, &local_tm);
    hour_   = static_cast<uint8_t>(local_tm.tm_hour);
    minute_ = static_cast<uint8_t>(local_tm.tm_min);
    second_ = static_cast<uint8_t>(local_tm.tm_sec);
}

HMSTime::operator std::string() const {
    return ToHMS();
}

std::string HMSTime::ToHMS(std::string_view separator) const {
    return COSMO_FORMAT("{:02}{}{:02}{}{:02}", hour_, separator, minute_, separator, second_);
}

std::string HMSTime::ToHM() const {
    return {DigitHi(hour_), DigitLo(hour_), ':', DigitHi(minute_), DigitLo(minute_)};
}

uint32_t HMSTime::ToInt() const {
    return static_cast<uint32_t>(hour_) * 3600 + static_cast<uint32_t>(minute_) * 60 +
           static_cast<uint32_t>(second_);
}

uint8_t HMSTime::Hour() const {
    return hour_;
}

uint8_t HMSTime::Minute() const {
    return minute_;
}

uint8_t HMSTime::Second() const {
    return second_;
}

// ---------------------------------------------------------------------------
// YMDDate
// ---------------------------------------------------------------------------

YMDDate::YMDDate() : year_{1970}, month_{1}, day_{1} {}

YMDDate::YMDDate(std::string_view str) : YMDDate{} {
    if (!str.empty()) {
        // Accept "YYYY-MM", "YYYY-MM-DD", and "YYYY-MM-DD HH:MM:SS".
        if (str.size() != 7 && str.size() != 10 && str.size() != 19) {
            LOG_INFO("{}", str);
            throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
        }
        // Construct null-terminated string for sscanf safety.
        std::string safe(str);
        int y{1970}, m{1}, d{1};
        int ret = sscanf(safe.c_str(), "%d-%d-%d", &y, &m, &d);
        if ((ret != 2 && ret != 3) || y >= 9999 || m > 12 || d > 31 || m == 0 || d == 0) {
            LOG_INFO("{}", str);
            throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
        }
        year_  = static_cast<uint16_t>(y);
        month_ = static_cast<uint8_t>(m);
        day_   = static_cast<uint8_t>(d);
    }
}

YMDDate::YMDDate(uint16_t y, uint8_t m, uint8_t d) : year_(y), month_(m), day_(d) {
    if (y >= 9999 || m > 12 || d > 31 || m == 0 || d == 0) {
        throw Exception(COSMO_FORMAT("wrong format, [{:04}-{:02}-{:02}]", y, m, d));
    }
}

YMDDate::YMDDate(std::time_t timestamp) : YMDDate{} {
    tm local_tm;
    localtime_r(&timestamp, &local_tm);
    year_  = static_cast<uint16_t>(local_tm.tm_year + 1900);
    month_ = static_cast<uint8_t>(local_tm.tm_mon + 1);
    day_   = static_cast<uint8_t>(local_tm.tm_mday);
}

YMDDate::operator std::string() const {
    return ToYMD();
}

std::string YMDDate::ToYMD(std::string_view separator) const {
    return COSMO_FORMAT("{:04}{}{:02}{}{:02}", year_, separator, month_, separator, day_);
}

std::string YMDDate::ToYMDZH() const {
    return COSMO_FORMAT("{:04}年{:02}月{:02}日", year_, month_, day_);
}

std::string YMDDate::ToYMZH() const {
    return COSMO_FORMAT("{:04}年{:02}月", year_, month_);
}

int YMDDate::WeekDay() const {
    tm t{};
    t.tm_year = year_ - 1900;
    t.tm_mon  = month_ - 1;
    t.tm_mday = day_;

    time_t ts = mktime(&t);
    localtime_r(&ts, &t);

    return t.tm_wday;
}

uint16_t YMDDate::Year() const {
    return year_;
}

uint8_t YMDDate::Month() const {
    return month_;
}

uint8_t YMDDate::Day() const {
    return day_;
}

// ---------------------------------------------------------------------------
// DateTime
// ---------------------------------------------------------------------------

DateTime::DateTime(const YMDDate& date, const HMSTime& time) : date_(date), time_(time) {}

DateTime::DateTime(std::time_t timestamp) : date_(timestamp), time_(timestamp) {}

DateTime::DateTime(std::string_view str) {
    auto str_parts = cosmo::util::Split(str, " ");
    switch (str_parts.size()) {
        case 0:
            return;
        case 1:
            if (str_parts[0].find('-') != std::string::npos)
                date_ = YMDDate(std::string_view(str_parts[0]));
            else
                time_ = HMSTime(std::string_view(str_parts[0]));
            return;
        case 2:
            date_ = YMDDate(std::string_view(str_parts[0]));
            time_ = HMSTime(std::string_view(str_parts[1]));
            return;
        default:
            break;
    }
    throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
}

time_t DateTime::ToTimeStamp() const {
    tm t{};
    t.tm_year = date_.Year() - 1900;
    t.tm_mon  = date_.Month() - 1;
    t.tm_mday = date_.Day();
    t.tm_hour = time_.Hour();
    t.tm_min  = time_.Minute();
    t.tm_sec  = time_.Second();

    return mktime(&t);
}

const YMDDate& DateTime::Date() const {
    return date_;
}

const HMSTime& DateTime::Time() const {
    return time_;
}

uint64_t DateTime::ToYMDHMSInt() const {
    return uint64_t(date_.Year()) * 10000000000 + date_.Month() * 100000000 + date_.Day() * 1000000 +
           time_.Hour() * 10000 + time_.Minute() * 100 + time_.Second();
}

// ---------------------------------------------------------------------------
// Comparison operators — HMSTime
// ---------------------------------------------------------------------------

bool operator==(const HMSTime& d1, const HMSTime& d2) {
    return d1.Hour() == d2.Hour() && d1.Minute() == d2.Minute() && d1.Second() == d2.Second();
}

bool operator<(const HMSTime& d1, const HMSTime& d2) {
    return d1.ToInt() < d2.ToInt();
}

bool operator<=(const HMSTime& d1, const HMSTime& d2) {
    return !(d2 < d1);
}

// ---------------------------------------------------------------------------
// Comparison operators — YMDDate
// ---------------------------------------------------------------------------

bool operator==(const YMDDate& y1, const YMDDate& y2) {
    return y1.Year() == y2.Year() && y1.Month() == y2.Month() && y1.Day() == y2.Day();
}

bool operator<(const YMDDate& y1, const YMDDate& y2) {
    if (y1.Year() != y2.Year())
        return y1.Year() < y2.Year();
    if (y1.Month() != y2.Month())
        return y1.Month() < y2.Month();
    return y1.Day() < y2.Day();
}

bool operator<=(const YMDDate& y1, const YMDDate& y2) {
    return !(y2 < y1);
}

// ---------------------------------------------------------------------------
// Comparison operators — DateTime
// ---------------------------------------------------------------------------

bool operator==(const DateTime& dt1, const DateTime& dt2) {
    return dt1.Date() == dt2.Date() && dt1.Time() == dt2.Time();
}

bool operator<(const DateTime& dt1, const DateTime& dt2) {
    return dt1.Date() < dt2.Date() || (dt1.Date() == dt2.Date() && dt1.Time() < dt2.Time());
}

bool operator<=(const DateTime& dt1, const DateTime& dt2) {
    return !(dt2 < dt1);
}

// ---------------------------------------------------------------------------
// Date arithmetic
// ---------------------------------------------------------------------------

YMDDate AddDay(const YMDDate& d1, int day) {
    time_t ts         = DateTime(d1, HMSTime{}).ToTimeStamp();
    uint16_t new_year = 0;
    uint8_t new_month = 0, new_day = 0;
    cosmo::util::GetDelayDate(day, ts, new_year, new_month, new_day);
    return YMDDate(new_year, new_month, new_day);
}

YMDDate AddMonth(const YMDDate& d1, int month) {
    time_t ts = DateTime(d1, HMSTime{}).ToTimeStamp();
    tm local_tm{};
    localtime_r(&ts, &local_tm);
    local_tm.tm_mon += month;
    return YMDDate(mktime(&local_tm));
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

DateTime GetCurrentDateTime() {
    time_t t = time(nullptr);
    return DateTime(YMDDate(t), HMSTime(t));
}

time_t TimeFromYMDHMSInt(uint64_t value) {
    tm t{};
    t.tm_year = static_cast<int>(value / 10000000000) - 1900;
    t.tm_mon  = static_cast<int>(value / 100000000 % 100) - 1;
    t.tm_mday = static_cast<int>(value / 1000000 % 100);
    t.tm_hour = static_cast<int>(value / 10000 % 100);
    t.tm_min  = static_cast<int>(value / 100 % 100);
    t.tm_sec  = static_cast<int>(value % 100);

    return mktime(&t);
}

DateTime DateTimeFromYMDHMSInt(uint64_t value) {
    return DateTime(TimeFromYMDHMSInt(value));
}

int GetYearDay(const YMDDate& date) {
    constexpr int kSecondsPerDay = 86400;
    return static_cast<int>(
        (DateTime(date).ToTimeStamp() - DateTime(YMDDate(date.Year(), 1, 1)).ToTimeStamp()) / kSecondsPerDay);
}

std::string GetYearWeek(const YMDDate& date) {
    int first_week_day = YMDDate(date.Year(), 1, 1).WeekDay();
    int weeks          = (GetYearDay(date) + first_week_day - 2) / 7;
    if (first_week_day > 4 || first_week_day == 0) {
        return weeks == 0 ? GetYearWeek(YMDDate(date.Year() - 1, 12, 31))
                          : COSMO_FORMAT("{}年第{}周", date.Year(), weeks);
    }
    return COSMO_FORMAT("{}年第{}周", date.Year(), weeks + 1);
}

DateTime GetDateTime(int64_t timestamp) {
    time_t t = timestamp / 1000;
    return DateTime(YMDDate(t), HMSTime(t));
}

}  // namespace cosmo::util
