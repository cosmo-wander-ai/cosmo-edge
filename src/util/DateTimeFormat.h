#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace cosmo::util {

/// Time-of-day value (hours, minutes, seconds).
class HMSTime {
public:
    /// Default constructs to 00:00:00.
    HMSTime();
    /// Parses "HH:MM:SS" or "HH:MM" format string.
    explicit HMSTime(std::string_view str);
    /// Constructs from h/m/s components. Throws if out of range [0,24),[0,60),[0,60).
    HMSTime(uint8_t h, uint8_t m, uint8_t s = 0);
    /// Constructs from seconds-since-midnight. Clamps to 23:59:59.
    explicit HMSTime(uint32_t sec);
    /// Constructs from a POSIX timestamp (extracts local time-of-day).
    explicit HMSTime(std::time_t timestamp);

    /// Explicit conversion to "HH:MM:SS" string.
    explicit operator std::string() const;

    /// Formats as "HH<sep>MM<sep>SS".
    std::string ToHMS(std::string_view separator = ":") const;
    /// Formats as "HH:MM".
    std::string ToHM() const;

    /// Converts to seconds since midnight.
    uint32_t ToInt() const;

    uint8_t Hour() const;
    uint8_t Minute() const;
    uint8_t Second() const;

private:
    uint8_t hour_;
    uint8_t minute_;
    uint8_t second_;
};

/// Calendar date value (year, month, day).
class YMDDate {
public:
    /// Default constructs to 1970-01-01 (epoch).
    YMDDate();
    /// Parses "YYYY-MM-DD", "YYYY-MM", or "YYYY-MM-DD HH:MM:SS" format string.
    explicit YMDDate(std::string_view str);
    /// Constructs from y/m/d components. Throws if out of valid range.
    YMDDate(uint16_t y, uint8_t m, uint8_t d = 1);
    /// Constructs from a POSIX timestamp (extracts local date).
    explicit YMDDate(std::time_t timestamp);

    /// Explicit conversion to "YYYY-MM-DD" string.
    explicit operator std::string() const;

    /// Formats as "YYYY<sep>MM<sep>DD".
    std::string ToYMD(std::string_view separator = "-") const;

    /// Formats as "YYYY年MM月DD日".
    std::string ToYMDZH() const;
    /// Formats as "YYYY年MM月".
    std::string ToYMZH() const;

    /// Returns day-of-week (0 = Sunday, 1 = Monday, ..., 6 = Saturday).
    int WeekDay() const;
    uint16_t Year() const;
    uint8_t Month() const;
    uint8_t Day() const;

private:
    uint16_t year_;
    uint8_t month_;
    uint8_t day_;
};

/// Combined date-and-time value.
class DateTime {
public:
    DateTime() = default;
    explicit DateTime(const YMDDate&, const HMSTime& = {});
    explicit DateTime(std::time_t timestamp);
    /// Parses "YYYY-MM-DD HH:MM:SS" format string.
    explicit DateTime(std::string_view str);

    time_t ToTimeStamp() const;

    const YMDDate& Date() const;
    const HMSTime& Time() const;

    /// Packs into YYYYMMDDHHmmss integer representation.
    uint64_t ToYMDHMSInt() const;

private:
    YMDDate date_;
    HMSTime time_;
};

// --- Comparison operators ---

bool operator==(const HMSTime& d1, const HMSTime& d2);
bool operator<(const HMSTime& d1, const HMSTime& d2);
bool operator<=(const HMSTime& d1, const HMSTime& d2);

bool operator==(const YMDDate& y1, const YMDDate& y2);
bool operator<(const YMDDate& y1, const YMDDate& y2);
bool operator<=(const YMDDate& y1, const YMDDate& y2);

bool operator==(const DateTime& dt1, const DateTime& dt2);
bool operator<(const DateTime& dt1, const DateTime& dt2);
bool operator<=(const DateTime& dt1, const DateTime& dt2);

// --- Arithmetic operators ---

// --- Date arithmetic ---

YMDDate AddDay(const YMDDate& d1, int day);
YMDDate AddMonth(const YMDDate& d1, int month);

// --- Integer encoding ---

time_t TimeFromYMDHMSInt(uint64_t value);
DateTime DateTimeFromYMDHMSInt(uint64_t value);

// --- Calendar utilities ---

/// Returns day-of-year (0-based) for the given date.
int GetYearDay(const YMDDate& date);
/// Returns ISO week string, e.g. "2025年第26周" (GB/T 7408-2005).
std::string GetYearWeek(const YMDDate& date);

/// Returns current local date/time.
DateTime GetCurrentDateTime();
/// Constructs DateTime from a millisecond timestamp.
DateTime GetDateTime(int64_t timestamp);

}  // namespace cosmo::util
