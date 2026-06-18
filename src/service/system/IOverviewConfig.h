/// @file IOverviewConfig.h
/// @brief Overview structure configuration interface.
///        ISP split from IAppInfoService.
///        Consumed by flow/overview/ and flow/common/ to check overview recording flags.
#pragma once

#include <string>

namespace cosmo::service {

/// Controls whether structured detection data is recorded to persistent
/// storage and/or written to overview data files during video analysis.
class IOverviewConfig {
public:
    virtual ~IOverviewConfig() = default;

    /// Enable or disable structured overview data recording to database.
    /// @param value true to enable recording.
    virtual void SetOverviewStructureRecord(bool value) = 0;

    /// Check whether structured overview data recording is enabled.
    virtual bool GetOverviewStructureRecord() = 0;

    /// Enable or disable structured overview data file output.
    /// @param value true to enable file output.
    virtual void SetOverviewStructureFile(bool value) = 0;

    /// Check whether structured overview data file output is enabled.
    virtual bool GetOverviewStructureFile() = 0;

    /// Get the base directory for task overview data files.
    virtual std::string GetTaskOverviewDataPath() = 0;
};

}  // namespace cosmo::service
