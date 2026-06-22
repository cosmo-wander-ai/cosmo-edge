// Narrow interface for camera–algorithm task configuration,
// notification, and scheduling operations.
// ISP split from ICameraService.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/media/dto/VideoFrameFwd.h"
#include "util/ErrorCode.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/CameraTaskDto.h"
#include "util/dto/TaskAreaTypes.h"

namespace cosmo::service {

/// Manages the binding between cameras and algorithms: parameter tuning,
/// detection area configuration, scheduling strategy, task switching,
/// algorithm change notification, and single-frame capture.
class ICameraTaskConfig {
public:
    virtual ~ICameraTaskConfig() = default;

    // ── Task Parameter Management ──

    /// Modify dynamic parameters for a camera–algorithm task.
    /// @param cameraId    Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @param params      Parameter configuration to apply.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ModifyTaskParam(const std::string& cameraId,
                                                   const std::string& algorithmId,
                                                   cosmo::MsgTaskConfig& params) = 0;

    /// Query current dynamic parameters for a camera–algorithm task.
    /// @param cameraId    Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @param params      [out] Current parameter key-value pairs.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum QueryTaskParam(const std::string& cameraId, const std::string& algorithmId,
                                                  std::vector<cosmo::MsgDynamicKeyValue>& params) = 0;

    /// Modify detection/shielded areas for a camera–algorithm task.
    /// @param cameraId      Camera identifier.
    /// @param algorithmId   Algorithm identifier.
    /// @param areas         Detection area polygons.
    /// @param shieldedAreas Shielded (excluded) area polygons (optional).
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ModifyTaskArea(
        const std::string& cameraId, const std::string& algorithmId,
        const std::vector<cosmo::MsgTaskArea>& areas,
        const std::vector<cosmo::MsgTaskArea>& shieldedAreas = {}) = 0;

    /// Query detection/shielded areas for a camera–algorithm task.
    /// @param cameraId      Camera identifier.
    /// @param algorithmId   Algorithm identifier.
    /// @param areas         [out] Detection area polygons.
    /// @param shieldedAreas [out] Shielded area polygons.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum QueryTaskArea(const std::string& cameraId, const std::string& algorithmId,
                                                 std::vector<cosmo::MsgTaskArea>& areas,
                                                 std::vector<cosmo::MsgTaskArea>& shieldedAreas) = 0;

    /// Bind a scheduling strategy to a camera–algorithm task.
    /// @param cameraId    Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @param scheduleId  Schedule template identifier to bind.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ModifyTaskStrategy(const std::string& cameraId,
                                                      const std::string& algorithmId,
                                                      const std::string& scheduleId) = 0;

    /// Query the scheduling strategy bound to a camera–algorithm task.
    /// @param cameraId    Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @param scheduleId  [out] Currently bound schedule template ID.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum QueryTaskStrategy(const std::string& cameraId,
                                                     const std::string& algorithmId,
                                                     std::string& scheduleId) = 0;

    /// Enable or disable a camera–algorithm task.
    /// @param cameraId    Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @param enable      true to enable, false to disable.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SwitchTask(const std::string& cameraId, const std::string& algorithmId,
                                              bool enable) = 0;

    /// Query the enable/disable state of a camera–algorithm task.
    /// @param cameraId    Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @param enable      [out] Current enable state.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum QuerySwitch(const std::string& cameraId, const std::string& algorithmId,
                                               bool& enable) = 0;

    /// Delete a camera–algorithm task binding.
    /// @param cameraId    Camera identifier.
    /// @param algorithmId Algorithm identifier.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum DeleteTask(const std::string& cameraId,
                                              const std::string& algorithmId) = 0;

    /// List all algorithm tasks bound to a camera.
    /// @param cameraId Camera identifier.
    /// @return Vector of camera task DTOs.
    virtual std::vector<service::camera::CameraTaskDto> GetTasks(const std::string& cameraId) = 0;

    // ── Algorithm Notification ──

    /// Notify that algorithm definitions have changed (e.g. model update).
    /// @param algorithmIds    List of affected algorithm IDs.
    /// @param restartRunning  If true, restart any currently running tasks using these algorithms.
    virtual void NotifyAlgorithmsChanged(const std::vector<std::string>& algorithmIds,
                                         bool restartRunning) = 0;

    /// Notify that algorithms have been deleted.
    /// @param algorithmIds List of deleted algorithm IDs.
    virtual void NotifyAlgorithmsDeleted(const std::vector<std::string>& algorithmIds) = 0;

    /// Check whether an algorithm is currently in use by any camera task.
    /// @param algorithmId Algorithm identifier.
    /// @return true if at least one camera task references this algorithm.
    virtual bool IsAlgorithmInUse(const std::string& algorithmId) const = 0;

    // ── Schedule and Capture ──

    /// Check whether a schedule template is currently in use by any camera task.
    /// @param scheduleId Schedule template identifier.
    /// @return true if at least one task references this schedule.
    virtual bool ScheduleInUse(const std::string& scheduleId) = 0;

    /// Capture a single image frame from a camera channel.
    /// @param channelId Channel identifier.
    /// @param timeOutMs Timeout in milliseconds (default: 3000).
    /// @return Captured video frame, or nullptr on timeout.
    virtual VideoFramePtr CaptureImage(const std::string& channelId, int timeOutMs = 3000) = 0;

    /// Bind face/body libraries to a camera–algorithm task parameter.
    /// @param cameraId      Camera identifier.
    /// @param algorithmCode Algorithm code.
    /// @param bindLibs      List of library IDs to bind.
    /// @param paramKey      Parameter key to associate the libraries with.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum BindTaskLibPara(const std::string& cameraId,
                                                   const std::string& algorithmCode,
                                                   const std::vector<std::string>& bindLibs,
                                                   const std::string& paramKey) = 0;
};

}  // namespace cosmo::service
