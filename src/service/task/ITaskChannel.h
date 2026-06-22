/// @file ITaskChannel.h
/// @brief Task channel interface — manages the binding between video
///        analysis tasks and camera channels, providing stream URL
///        configuration, frame capture, and alarm instance access.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/media/dto/VideoFrameFwd.h"
#include "util/dto/CameraMsgTypes.h"

namespace cosmo {
class AlgChannel;
using AlgChannelPtr = std::shared_ptr<AlgChannel>;
class TaskAlarm;
using TaskAlarmPtr = std::shared_ptr<TaskAlarm>;
}  // namespace cosmo

namespace cosmo::service {

/// Runtime interface for task–channel bindings.
///
/// Provides channel URL and parameter configuration, single-frame capture,
/// channel attribute queries, task-to-channel mapping, and access to
/// algorithm channel and alarm instances.
class ITaskChannel {
public:
    virtual ~ITaskChannel() = default;

    // ── Channel Configuration ──

    /// Set the stream URL for a task channel.
    /// @param channelId Camera channel identifier.
    /// @param url       RTSP or file URL.
    virtual void TaskChannelSetUrl(const std::string& channelId, const std::string& url) = 0;

    /// Set stream URL and video repeat count for a task channel.
    /// @param channelId        Camera channel identifier.
    /// @param url              RTSP or file URL.
    /// @param videoRepeatCount Number of times to loop a VoD file (0 = infinite).
    virtual void TaskChannelSetParam(const std::string& channelId, const std::string& url,
                                     int videoRepeatCount) = 0;

    // ── Frame Capture ──

    /// Capture a single image frame from a camera channel.
    /// @param channelId Camera channel identifier.
    /// @param timeOutMs Timeout in milliseconds (default: 3000).
    /// @return Captured video frame, or nullptr on timeout.
    virtual VideoFramePtr CaptureImage(const std::string& channelId, int timeOutMs = 3000) = 0;

    // ── Channel Queries ──

    /// Get channel attributes (resolution, FPS, codec).
    /// @param channelId Camera channel identifier.
    /// @param attr      [out] Channel attributes.
    /// @return true if the channel was found.
    virtual bool GetChannelAttr(const std::string& channelId, cosmo::MsgCameraAttr& attr) = 0;

    /// Check whether a channel's data pipeline is actively receiving data.
    /// @param channelId Camera channel identifier.
    /// @return true if data is flowing.
    virtual bool TaskDataActive(const std::string& channelId) = 0;

    // ── Instance Access ──

    /// Get the AlgChannel instance for a channel.
    /// @param channelId Camera channel identifier.
    /// @return Shared pointer to the algorithm channel.
    virtual cosmo::AlgChannelPtr GetChannelInst(const std::string& channelId) = 0;

    /// Get all task IDs bound to a channel.
    /// @param channelId Camera channel identifier.
    /// @return Vector of task IDs.
    virtual std::vector<std::string> GetChannelTasks(const std::string& channelId) = 0;

    /// Get the alarm instance for a channel–task pair.
    /// @param channelId Camera channel identifier.
    /// @param taskId    Task identifier.
    /// @return Shared pointer to the task alarm.
    virtual cosmo::TaskAlarmPtr GetAlarmInst(const std::string& channelId, const std::string& taskId) = 0;

    /// Get the channel ID that a task is bound to.
    /// @param taskId Task identifier.
    /// @return Channel ID, or empty if not found.
    virtual std::string GetTaskChannel(const std::string& taskId) = 0;

    /// Get camera info for all channels.
    /// @param cameraInfos [out] Vector of camera information structures.
    virtual void GetCameraInfo(std::vector<cosmo::MsgCameraInfo>& cameraInfos) = 0;
};

// Dependency injection — narrow interface access

}  // namespace cosmo::service
