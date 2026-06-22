/// @file IAlarmPushService.h
/// @brief Interface for alarm event push — config and lifecycle.
#pragma once

#include <string>

#include "util/ErrorCode.h"

namespace cosmo::service {

/// Manages pushing alarm events to an external HTTP server.
class IAlarmPushService {
public:
    virtual ~IAlarmPushService() = default;

    /// Initialize the alarm push subsystem (timers, event queue binding).
    virtual void Init() = 0;

    /// Check whether alarm push is currently enabled.
    virtual bool IsEnabled() = 0;

    /// Get the configured push target URL.
    virtual std::string GetUrl() = 0;

    /// Enable or disable alarm push and set the target URL.
    virtual cosmo::util::ErrorEnum SetPush(bool enable, const std::string& url) = 0;
};

}  // namespace cosmo::service
