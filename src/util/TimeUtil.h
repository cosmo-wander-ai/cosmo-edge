#pragma once

#include <chrono>
#include <string>

namespace cosmo::util {
/// Get current timestamp in milliseconds from system clock
inline int64_t GetMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Get current timestamp in milliseconds from steady clock
inline int64_t GetMillisecondsFromSteadyClock() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Convert system_clock time_point to timestamp in milliseconds
inline int64_t ToMilliseconds(const std::chrono::system_clock::time_point& t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}

inline int64_t ToMilliseconds(const std::chrono::steady_clock::time_point& t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}

/// Calculate a future or past date based on a delay in days
void GetDelayDate(int delayDays, time_t currentTime, uint16_t& year, uint8_t& month, uint8_t& day);

}  // namespace cosmo::util
