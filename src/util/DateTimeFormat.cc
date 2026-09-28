// DateTimeFormat — Date Time Format implementation.

#include "util/DateTimeFormat.h"

#include <limits>

#include "util/Exception.h"
#include "util/FormatString.h"
#include "util/Log.h"
#include "util/TimeUtil.h"

namespace {

/// Converts a digit pair to two ASCII characters (e.g., 12 → '1','2').
constexpr char DigitHi(uint8_t n) {
    return static_cast<char>(n / 10 + '0');
}
constexpr char DigitLo(uint8_t n) {
    return static_cast<char>(n % 10 + '0');
}

constexpr bool IsAsciiDigit(char value) {
    return value >= '0' && value <= '9';
}

uint32_t ParseDigits(std::string_view value, size_t offset, size_t count) {
    if (offset > value.size() || count > value.size() - offset) {
        throw cosmo::util::Exception(COSMO_FORMAT("wrong format, [{}]", value));
    }

    uint32_t result = 0;
    for (size_t index = offset; index < offset + count; ++index) {
        if (!IsAsciiDigit(value[index])) {
            throw cosmo::util::Exception(COSMO_FORMAT("wrong format, [{}]", value));
        }
        result = result * 10 + static_cast<uint32_t>(value[index] - '0');
    }
    return result;
}

constexpr bool IsLeapYear(uint16_t year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr uint8_t DaysInMonth(uint16_t year, uint8_t month) {
    constexpr uint8_t kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12) {
        return 0;
    }
    if (month == 2 && IsLeapYear(year)) {
        return 29;
    }
    return kMonthDays[month - 1];
}

[[noreturn]] void ThrowInvalidTimestamp() {
    throw cosmo::util::Exception("timestamp cannot be represented as local time");
}

}  // namespace

namespace cosmo::util {

// ---------------------------------------------------------------------------
// HMSTime
// ---------------------------------------------------------------------------

HMSTime::HMSTime() : hour_{0}, minute_{0}, second_{0} {}

HMSTime::HMSTime(std::string_view str) : HMSTime{} {
    if (!str.empty()) {
        if ((str.size() != 5 && str.size() != 8) || str[2] != ':' || (str.size() == 8 && str[5] != ':')) {
            throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
        }

        const auto h = ParseDigits(str, 0, 2);
        const auto m = ParseDigits(str, 3, 2);
        const auto s = str.size() == 8 ? ParseDigits(str, 6, 2) : 0;
        if (h >= 24 || m >= 60 || s >= 60) {
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
    tm local_tm{};
    if (localtime_r(&timestamp, &local_tm) == nullptr) {
        ThrowInvalidTimestamp();
    }
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
        if ((str.size() != 7 && str.size() != 10 && str.size() != 19) || str[4] != '-' ||
            (str.size() >= 10 && str[7] != '-')) {
            LOG_INFO("{}", str);
            throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
        }

        const auto y = ParseDigits(str, 0, 4);
        const auto m = ParseDigits(str, 5, 2);
        const auto d = str.size() >= 10 ? ParseDigits(str, 8, 2) : 1;
        if (y == 0 || y >= 9999 || m == 0 || m > 12 || d == 0 ||
            d > DaysInMonth(static_cast<uint16_t>(y), static_cast<uint8_t>(m))) {
            LOG_INFO("{}", str);
            throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
        }

        if (str.size() == 19) {
            if (str[10] != ' ') {
                throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
            }
            (void)HMSTime(str.substr(11));
        }
        year_  = static_cast<uint16_t>(y);
        month_ = static_cast<uint8_t>(m);
        day_   = static_cast<uint8_t>(d);
    }
}

YMDDate::YMDDate(uint16_t y, uint8_t m, uint8_t d) : year_(y), month_(m), day_(d) {
    if (y == 0 || y >= 9999 || m == 0 || m > 12 || d == 0 || d > DaysInMonth(y, m)) {
        throw Exception(COSMO_FORMAT("wrong format, [{:04}-{:02}-{:02}]", y, m, d));
    }
}

YMDDate::YMDDate(std::time_t timestamp) : YMDDate{} {
    tm local_tm{};
    if (localtime_r(&timestamp, &local_tm) == nullptr) {
        ThrowInvalidTimestamp();
    }
    if (local_tm.tm_year < -1899 || local_tm.tm_year > 8098) {
        ThrowInvalidTimestamp();
    }
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
    const auto timestamp = DateTime(*this).ToTimeStamp();
    tm local_tm{};
    if (localtime_r(&timestamp, &local_tm) == nullptr) {
        ThrowInvalidTimestamp();
    }
    return local_tm.tm_wday;
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
    if ((str.size() == 7 || str.size() == 10) && str.find('-') != std::string_view::npos) {
        date_ = YMDDate(str);
        return;
    }
    if ((str.size() == 5 || str.size() == 8) && str.find(':') != std::string_view::npos) {
        time_ = HMSTime(str);
        return;
    }
    if (str.size() == 19 && str[10] == ' ') {
        date_ = YMDDate(str.substr(0, 10));
        time_ = HMSTime(str.substr(11));
        return;
    }
    throw Exception(COSMO_FORMAT("wrong format, [{}]", str));
}

time_t DateTime::ToTimeStamp() const {
    tm t{};
    t.tm_year  = static_cast<int>(date_.Year()) - 1900;
    t.tm_mon   = static_cast<int>(date_.Month()) - 1;
    t.tm_mday  = static_cast<int>(date_.Day());
    t.tm_hour  = static_cast<int>(time_.Hour());
    t.tm_min   = static_cast<int>(time_.Minute());
    t.tm_sec   = static_cast<int>(time_.Second());
    t.tm_isdst = -1;

    const auto timestamp = mktime(&t);
    tm round_trip{};
    if (localtime_r(&timestamp, &round_trip) == nullptr ||
        round_trip.tm_year != static_cast<int>(date_.Year()) - 1900 ||
        round_trip.tm_mon != static_cast<int>(date_.Month()) - 1 ||
        round_trip.tm_mday != static_cast<int>(date_.Day()) ||
        round_trip.tm_hour != static_cast<int>(time_.Hour()) ||
        round_trip.tm_min != static_cast<int>(time_.Minute()) ||
        round_trip.tm_sec != static_cast<int>(time_.Second())) {
        ThrowInvalidTimestamp();
    }
    return timestamp;
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
    if (localtime_r(&ts, &local_tm) == nullptr) {
        ThrowInvalidTimestamp();
    }
    if ((month > 0 && local_tm.tm_mon > std::numeric_limits<int>::max() - month) ||
        (month < 0 && local_tm.tm_mon < std::numeric_limits<int>::min() - month)) {
        throw Exception("month offset is out of range");
    }
    local_tm.tm_mon += month;
    const auto result = mktime(&local_tm);
    return YMDDate(result);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

DateTime GetCurrentDateTime() {
    time_t t = time(nullptr);
    return DateTime(YMDDate(t), HMSTime(t));
}

time_t TimeFromYMDHMSInt(uint64_t value) {
    const auto year   = value / 10000000000ULL;
    const auto month  = value / 100000000ULL % 100;
    const auto day    = value / 1000000ULL % 100;
    const auto hour   = value / 10000ULL % 100;
    const auto minute = value / 100ULL % 100;
    const auto second = value % 100;

    if (year > std::numeric_limits<uint16_t>::max() || month > std::numeric_limits<uint8_t>::max() ||
        day > std::numeric_limits<uint8_t>::max() || hour > std::numeric_limits<uint8_t>::max() ||
        minute > std::numeric_limits<uint8_t>::max() || second > std::numeric_limits<uint8_t>::max()) {
        throw Exception("packed date/time is out of range");
    }

    return DateTime(
               YMDDate(static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)),
               HMSTime(static_cast<uint8_t>(hour), static_cast<uint8_t>(minute),
                       static_cast<uint8_t>(second)))
        .ToTimeStamp();
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
    // ISO 8601 / GB/T 7408-2005: weeks run Monday..Sunday and week 1 contains
    // the first Thursday of the year, so early January may belong to the
    // previous year and late December to the next year.
    int day_of_year = GetYearDay(date);              // 0-based
    int week_day    = date.WeekDay();                // 0=Sunday..6=Saturday
    int iso_dow     = week_day == 0 ? 7 : week_day;  // 1=Monday..7=Sunday
    int week        = (day_of_year + 11 - iso_dow) / 7;
    if (week < 1) {
        return GetYearWeek(YMDDate(date.Year() - 1, 12, 31));
    }
    if (week == 53) {
        // Week 53 exists only when Jan 1 is a Thursday, or a Wednesday of a leap year.
        int jan1_week_day = YMDDate(date.Year(), 1, 1).WeekDay();
        int jan1_iso_dow  = jan1_week_day == 0 ? 7 : jan1_week_day;
        bool has_week_53  = jan1_iso_dow == 4 || (IsLeapYear(date.Year()) && jan1_iso_dow == 3);
        if (!has_week_53) {
            return COSMO_FORMAT("{}年第{}周", date.Year() + 1, 1);
        }
    }
    return COSMO_FORMAT("{}年第{}周", date.Year(), week);
}

DateTime GetDateTime(int64_t timestamp) {
    time_t t = timestamp / 1000;
    return DateTime(YMDDate(t), HMSTime(t));
}

}  // namespace cosmo::util
