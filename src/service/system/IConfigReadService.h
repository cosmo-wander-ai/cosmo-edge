/// @file IConfigReadService.h
/// @brief Read-only system configuration service interface.
///        ISP split from ISystemConfigService.
///        Provides query-only access to alarm, reboot, logo, debug, popup,
///        run-mode, and resource-limit settings.
///        Most flow/ consumers only need this interface.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/system/dto/SystemConfigDto.h"
#include "service/system/dto/SystemConfigNetworkDto.h"
#include "service/system/dto/SystemConfigTypes.h"

namespace cosmo::service {

/// Read-only access to system configuration parameters.
///
/// Consumers in the flow layer that need to check system settings
/// (debug mode, run mode, picture quality, etc.) should depend on this
/// narrow interface rather than the full writable config service.
class IConfigReadService {
public:
    virtual ~IConfigReadService() = default;

    // ── Picture Quality ──

    /// Get the current picture quality / alarm image settings.
    virtual cosmo::CfgAlarmParamOverviewInfo GetPictureQuality() = 0;

    // ── Alarm Recording ──

    /// Get the alarm video recording duration parameters.
    virtual cosmo::CfgAlarmParamVideoRecordInfo GetAlarmVideoDuration() = 0;

    // ── Scheduled Restart ──

    /// Get the scheduled device restart configuration.
    virtual cosmo::CfgRebootParamInfo GetRebootParam() = 0;

    // ── Logo ──

    /// Get the current system logo configuration.
    virtual SystemLogoInfo GetSystemLogo() = 0;

    // ── Debug Mode ──

    /// Check whether debug mode is enabled.
    virtual bool GetDebugMode() = 0;

    /// Get the list of shielded (disabled) action IDs.
    virtual std::vector<std::string> GetShieldedActions() = 0;

    /// Check whether a specific action is enabled.
    /// @param actionId Action identifier to check.
    /// @return true if the action is enabled (not shielded).
    virtual bool GetActionSwitch(const std::string& actionId) = 0;

    // ── Popup Parameters ──

    /// Get alarm popup notification parameters.
    /// @param popUpSwitch  [out] Popup enabled (1) or disabled (0).
    /// @param audioPlay    [out] Audio play enabled (1) or disabled (0).
    /// @param popUpDuration [out] Popup display duration in seconds.
    virtual void GetPopUpParam(int& popUpSwitch, int& audioPlay, int& popUpDuration) = 0;

    // ── Run Mode & Resource ──

    /// Get the current run mode (standalone / IoT managed).
    virtual cosmo::RunMode GetRunMode() = 0;

    /// Check whether resource limiting is enabled.
    virtual bool GetResourceLimit() = 0;

    /// Check whether the device is operating in network (IoT) model.
    virtual bool IsNetworkModel() = 0;
};

}  // namespace cosmo::service
