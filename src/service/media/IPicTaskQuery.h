/// @file IPicTaskQuery.h
/// @brief Image analysis task query, parameter, and status interface.
///        ISP split from IPicTaskService.
#pragma once

#include <string>
#include <vector>

#include "util/dto/TaskAreaTypes.h"
#include "util/dto/TaskStatusDto.h"

namespace cosmo::service {

/// Query, parameter, and status operations for image analysis tasks.
class IPicTaskQuery {
public:
    virtual ~IPicTaskQuery() = default;

    // ── Task Query ──

    /// Query all task IDs, optionally filtering by started state.
    /// @param started If true, return only started tasks.
    /// @return Vector of task IDs.
    virtual std::vector<std::string> QueryTasks(bool started = false) = 0;

    /// Query real (non-virtual) task IDs, optionally filtering by started state.
    /// @param started If true, return only started tasks.
    /// @return Vector of task IDs.
    virtual std::vector<std::string> QueryRealTasks(bool started = false) = 0;

    // ── Task Parameters ──

    /// Set task parameters.
    /// @param taskId Task identifier.
    /// @param param  Parameter configuration to apply.
    /// @return true on success.
    virtual bool SetTaskParam(const std::string& taskId, cosmo::MsgTaskConfig& param) = 0;

    /// Get current task parameters.
    /// @param taskId Task identifier.
    /// @param param  [out] Current parameter configuration.
    /// @return true on success.
    virtual bool GetTaskParam(const std::string& taskId, cosmo::MsgTaskConfig& param) = 0;

    // ── Task Status ──

    /// Get status information for all image tasks.
    /// @param durationSec Time window for statistics (default: 30s).
    /// @return Vector of task status DTOs.
    virtual std::vector<cosmo::PTaskStatus> GetTaskStatus(unsigned int durationSec = 30) = 0;

    /// Check if task configuration has changed compared to a given checksum.
    /// @param nodeAlgorithmCheckSum Checksum to compare against.
    /// @return true if the current configuration differs.
    virtual bool TasksHaveChange(const std::string& nodeAlgorithmCheckSum) = 0;

    /// Get the total number of image tasks.
    virtual size_t TaskCount() = 0;
};

}  // namespace cosmo::service
