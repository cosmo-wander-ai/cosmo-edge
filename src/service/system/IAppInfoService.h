/// @file IAppInfoService.h
/// @brief Aggregate application information service interface.
///        Inherits all ISP sub-interfaces (IOverviewConfig, IHardwareQuery,
///        IMemoryDiag). Callers should prefer the narrow sub-interfaces for new code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/system/IHardwareQuery.h"
#include "service/system/IMemoryDiag.h"
#include "service/system/IOverviewConfig.h"
#include "service/task/dto/StatusMsgTypes.h"
#include "util/dto/ServerMsgTypes.h"

namespace cosmo::service {

/// Aggregate application info service providing runtime metadata,
/// configuration, and system overview data.
///
/// Inherits narrow sub-interfaces for hardware queries, memory diagnostics,
/// and overview configuration.  New code should prefer the narrow interfaces.
class IAppInfoService : public IOverviewConfig, public IHardwareQuery, public IMemoryDiag {
public:
    virtual ~IAppInfoService() = default;

    // ── App Runtime ──

    /// Check whether the device is managed by a platform server.
    virtual bool GetHaveManager() = 0;

    /// Get the engine type identifier (e.g. "BM1688").
    virtual std::string GetEngineType() = 0;

    /// Get the device unique identifier.
    virtual std::string DevId() = 0;

    /// Get the application uptime in seconds.
    virtual int64_t GetAppRuntime() = 0;

    /// Get the number of image task groups.
    virtual int GetPicTaskGroupCount() = 0;

    /// Get the user data directory path.
    virtual std::string UserDataPath() = 0;

    /// Check whether model debug mode is enabled.
    virtual bool GetModelDebug() = 0;

    /// Get a configuration number value.
    virtual size_t GetNumber() = 0;

    /// Get the log file directory path.
    virtual std::string LogPath() = 0;

    /// Get the web-accessible log directory path.
    virtual std::string LogWebPath() = 0;

    // ── Model Path Utility ──

    /// Register a model path mapping for an algorithm code.
    /// @param algCode   Algorithm code identifier.
    /// @param modelPath Absolute path to the model directory.
    virtual void SetModelPath(const std::string& algCode, const std::string& modelPath) = 0;

    // ── System Overviews and Logging ──

    /// Query system logs with pagination.
    /// @param data Query parameters including page number, page size, and filters.
    /// @param errc [out] Error condition if query fails.
    /// @return Response payload with log entries.
    virtual cosmo::MsgQueryLogsSend GetPagedLogs(const cosmo::MsgQueryLogsRecv& data,
                                                 std::error_condition& errc) = 0;

    /// Get a comprehensive system overview (CPU, memory, disk, tasks, etc.).
    /// @param data Query parameters.
    /// @param errc [out] Error condition if query fails.
    /// @return Response payload with system overview data.
    virtual cosmo::MsgInfoSend GetSystemOverviewInfo(const cosmo::MsgInfoRecv& data,
                                                     std::error_condition& errc) = 0;
};

}  // namespace cosmo::service
