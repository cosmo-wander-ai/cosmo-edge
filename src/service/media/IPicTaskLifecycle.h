/// @file IPicTaskLifecycle.h
/// @brief Image analysis task lifecycle management interface.
///        ISP split from IPicTaskService.
#pragma once

#include <string>

#include "util/ErrorCode.h"
#include "util/dto/CosmoFwd.h"

namespace cosmo::service {

/// Lifecycle operations for image analysis tasks: create, delete, start.
class IPicTaskLifecycle {
public:
    virtual ~IPicTaskLifecycle() = default;

    /// Create a new image analysis task.
    /// @param taskId    Unique task identifier.
    /// @param actionAlg Algorithm action configuration.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum TaskCreate(const std::string& taskId, cosmo::ActionAlgPtr actionAlg) = 0;

    /// Delete an image analysis task.
    /// @param taskId Task identifier to delete.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum TaskDelete(const std::string& taskId) = 0;

    /// Start an image analysis task (load models, prepare pipeline).
    /// @param taskId Task identifier.
    /// @return true on success.
    virtual bool TaskStart(const std::string& taskId) = 0;

    /// Delete all image analysis tasks.
    virtual void TaskDeleteAll() = 0;

    /// Update the checksum for algorithm synchronization.
    /// @param nodeAlgorithmCheckSum New checksum value.
    virtual void UpdateCheckSum(const std::string& nodeAlgorithmCheckSum) = 0;

    /// Get the current algorithm checksum.
    /// @return Checksum string.
    virtual std::string GetCheckSum() = 0;
};

}  // namespace cosmo::service
