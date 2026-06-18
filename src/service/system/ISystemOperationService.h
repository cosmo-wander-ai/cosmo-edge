/// @file ISystemOperationService.h
/// @brief System operation service interface — reboot, factory reset,
///        log export, firmware upgrade, and debug tools.
#pragma once

#include <string>

#include "service/detail/ServiceRegistry.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

/// Provides system-level operations: device reboot, factory reset,
/// log archive export, firmware upgrade, and debug diagnostics.
class ISystemOperationService {
public:
    virtual ~ISystemOperationService() = default;

    // ── System Operation ──

    /// Reboot the device.
    /// @param reason Human-readable reason for the reboot (logged).
    virtual void RebootDevice(const std::string& reason) = 0;

    /// Factory-reset the device (clears all configuration and data).
    /// @param reason Human-readable reason for the reset (logged).
    virtual void ResetDevice(const std::string& reason) = 0;

    // ── File Export ──

    /// Export system logs as a downloadable archive.
    /// @param fileName [out] Generated archive file name.
    /// @param fileUrl  [out] Web-accessible URL to download the archive.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ExportLogs(std::string& fileName, std::string& fileUrl) = 0;

    /// Apply a firmware upgrade from a local file.
    /// @param filePath Path to the upgrade package.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Upgrade(const std::string& filePath) = 0;

    // ── Debug Tools ──

    /// Dump thread status information to the log for debugging.
    virtual void ShowThreadDebugInfo() = 0;
};

// Dependency injection

}  // namespace cosmo::service
