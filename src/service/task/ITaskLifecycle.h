/// @file ITaskLifecycle.h
/// @brief Task lifecycle interface — create, delete, start, stop,
///        parameter management, task orchestration, and data recording
///        for video analysis tasks.
#pragma once

#include <string>

#include "service/detail/ServiceRegistry.h"
#include "util/ErrorCode.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/CosmoFwd.h"
#include "util/dto/ServerMsgTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo::service {

/// Provides core lifecycle operations for video analysis tasks.
///
/// Consumed by the camera service layer for task orchestration during
/// camera add/update/delete and algorithm change notifications, and by
/// API message handlers for task create/cancel requests.
class ITaskLifecycle {
public:
    virtual ~ITaskLifecycle() = default;

    /// Create a new video analysis task.
    /// @param channelId   Camera channel identifier.
    /// @param channelName Camera channel display name.
    /// @param taskId      Unique task identifier.
    /// @param actionAlg   Algorithm action configuration.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum TaskCreate(const std::string& channelId, const std::string& channelName,
                                              const std::string& taskId, cosmo::ActionAlgPtr actionAlg) = 0;

    /// Delete a video analysis task.
    /// @param taskId Task identifier to delete.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum TaskDelete(const std::string& taskId) = 0;

    // ── Task Process Orchestration ──

    /// Process a task creation request from the platform.
    /// Fetches algorithm config, downloads stream URL, creates and starts the task.
    /// @param data Request payload.
    /// @param errc [out] Error condition if creation fails.
    /// @return Response payload.
    virtual cosmo::MsgTaskCreateSend ProcessTaskCreate(cosmo::MsgTaskCreateRecv& data,
                                                       std::error_condition& errc) = 0;

    /// Process a task cancellation request from the platform.
    /// @param data Request payload.
    /// @param errc [out] Error condition if cancellation fails.
    /// @return Response payload.
    virtual cosmo::MsgTaskCancleSend ProcessTaskCancel(cosmo::MsgTaskCancleRecv& data,
                                                       std::error_condition& errc) = 0;

    /// Start a video analysis task (begin stream processing).
    /// @param channelId Camera channel identifier.
    /// @param taskId    Task identifier.
    /// @return true on success.
    virtual bool TaskStart(const std::string& channelId, const std::string& taskId) = 0;

    /// Stop a running video analysis task.
    /// @param taskId Task identifier.
    /// @return true on success.
    virtual bool TaskStop(const std::string& taskId) = 0;

    /// Check whether a task is currently started.
    /// @param taskId Task identifier.
    /// @return true if the task is running.
    virtual bool TaskIsStart(const std::string& taskId) = 0;

    // ── Task Parameters ──

    /// Set task parameters (detection thresholds, areas, etc.).
    /// @param channelId Camera channel identifier.
    /// @param taskId    Task identifier.
    /// @param param     Parameter configuration to apply.
    /// @return true on success.
    virtual bool SetTaskParam(const std::string& channelId, const std::string& taskId,
                              cosmo::MsgTaskConfig& param) = 0;

    /// Run a logic test on a task with a simulated detection target.
    /// @param taskId Task identifier.
    /// @param target Simulated target for logic testing.
    /// @return true if the logic pipeline accepted the target.
    virtual bool LogicTest(const std::string& taskId, cosmo::MsgTarget& target) = 0;

    /// Log the action graph of an algorithm for debugging.
    /// @param actionAlg Algorithm action configuration to display.
    virtual void ShowActions(cosmo::ActionAlgPtr actionAlg) = 0;

    // ── Data Recording ──

    /// Clear all recorded data for a task.
    /// @param taskId Task identifier whose records should be removed.
    virtual void RecordClearTaskData(const std::string& taskId) = 0;

    /// Record task creation parameters.
    /// @param taskId Task identifier.
    /// @param data   Task creation request data to record.
    virtual void RecordTaskInfo(const std::string& taskId, cosmo::MsgTaskCreateRecv& data) = 0;

    /// Record the algorithm action configuration used by a task.
    /// @param taskId Task identifier.
    /// @param data   Algorithm action configuration to record.
    virtual void RecordTaskAction(const std::string& taskId, cosmo::ActionAlgPtr data) = 0;
};

}  // namespace cosmo::service
