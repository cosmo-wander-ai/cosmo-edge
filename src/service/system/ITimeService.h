/// @file ITimeService.h
/// @brief Time service interface — system time management, NTP synchronization,
///        timezone configuration, and manual time setting.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// NTP (Network Time Protocol) client configuration.
struct NtpConfig {
    int enable{0};       ///< NTP enabled (1) or disabled (0).
    std::string server;  ///< NTP server hostname or IP.
    short port{123};     ///< NTP server port (default: 123).
    int interval{60};    ///< Synchronization interval in seconds.
};

/// Timezone descriptor for UI selection.
struct TimeZoneItem {
    std::string name;   ///< Timezone display name (e.g. "Asia/Shanghai").
    std::string value;  ///< Timezone offset value (e.g. "+08:00").
    int id{0};          ///< Timezone numeric identifier.
};

/// Current system time status snapshot.
struct TimeStatus {
    int64_t timestamp{0};       ///< Current epoch timestamp (seconds).
    std::string timeString;     ///< Formatted time string.
    std::string timeZoneValue;  ///< Active timezone offset value.
    int timeZoneId{75};         ///< Active timezone numeric ID.
    NtpConfig ntp;              ///< Current NTP configuration.
};

/// Manages system clock: queries current time/timezone, synchronizes
/// via NTP, and allows manual time setting.
class ITimeService {
public:
    virtual ~ITimeService() = default;

    /// Get the current time status and available timezone list.
    /// @param zones [out] Available timezone list.
    /// @return Current time status snapshot.
    virtual TimeStatus GetTimeStatus(std::vector<TimeZoneItem>& zones) = 0;

    /// Synchronize the system clock via NTP.
    /// @param config     NTP client configuration.
    /// @param timeZoneId Target timezone numeric ID.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SyncNtp(const NtpConfig& config, int timeZoneId) = 0;

    /// Manually set the system time.
    /// @param timestamp  Epoch timestamp to set (milliseconds).
    /// @param timeZoneId Target timezone numeric ID.
    /// @return ErrorEnum::Success on success.
    virtual cosmo::util::ErrorEnum SetTime(int64_t timestamp, int timeZoneId) = 0;
};

// Dependency injection

}  // namespace cosmo::service
