/// @file IConfigWriteService.h
/// @brief Write/reset system configuration service interface.
///        ISP split from ISystemConfigService.
///        Provides mutation access to alarm, reboot, logo, debug, popup,
///        run-mode, and resource-limit settings.
///        Only consumed by api/ handlers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/system/dto/SystemConfigDto.h"
#include "service/system/dto/SystemConfigNetworkDto.h"
#include "service/system/dto/SystemConfigTypes.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Write and reset operations for system configuration parameters.
///
/// Only API handlers should depend on this interface; flow/ consumers
/// that only need to read settings should use IConfigReadService instead.
class IConfigWriteService {
public:
    virtual ~IConfigWriteService() = default;

    // ── Picture Quality ──

    /// Set picture quality / alarm image parameters.
    /// @param info Updated quality parameters.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SetPictureQuality(cosmo::CfgAlarmParamOverviewInfo info) = 0;

    /// Reset picture quality parameters to factory defaults.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ResetPictureQuality() = 0;

    // ── Alarm Recording ──

    /// Set alarm video recording duration parameters.
    /// @param info Updated recording parameters.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SetAlarmVideoDuration(cosmo::CfgAlarmParamVideoRecordInfo info) = 0;

    /// Reset alarm video recording parameters to factory defaults.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ResetAlarmVideoDuration() = 0;

    // ── Scheduled Restart ──

    /// Set scheduled device restart parameters.
    /// @param info Updated reboot schedule.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SetRebootParam(cosmo::CfgRebootParamInfo info) = 0;

    /// Reset scheduled restart parameters to factory defaults.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ResetRebootParam() = 0;

    // ── Logo ──

    /// Set the system logo and branding.
    /// @param systemName    System display name.
    /// @param logoFileType  Logo image MIME type (e.g. "image/png").
    /// @param logoImg       Logo image binary data.
    /// @param bigScreenName Large display title text.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SetSystemLogo(const std::string& systemName,
                                                 const std::string& logoFileType,
                                                 const std::vector<uint8_t>& logoImg,
                                                 const std::string& bigScreenName) = 0;

    // ── Debug Mode ──

    /// Enable or disable debug mode.
    /// @param enable true to enable debug mode.
    virtual void SetDebugMode(bool enable) = 0;

    /// Set the list of shielded (disabled) action IDs.
    /// @param actions List of action IDs to disable.
    virtual void SetShieldedActions(const std::vector<std::string>& actions) = 0;

    // ── Popup Parameters ──

    /// Set alarm popup notification parameters.
    /// @param popUpSwitch   Popup enabled (1) or disabled (0).
    /// @param audioPlay     Audio play enabled (1) or disabled (0).
    /// @param popUpDuration Popup display duration in seconds.
    virtual void SetPopUpParam(int popUpSwitch, int audioPlay, int popUpDuration) = 0;

    // ── Run Mode & Resource ──

    /// Set the device run mode.
    /// @param mode Target run mode (standalone / IoT managed).
    virtual void SetRunMode(cosmo::RunMode mode) = 0;

    /// Enable or disable resource limiting.
    /// @param enable true to enable resource limits.
    virtual void SetResourceLimit(bool enable) = 0;
};

}  // namespace cosmo::service
