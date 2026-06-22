/// @file ITaskQuery.h
/// @brief Task query interface — read-only access to video task status,
///        frame info, queue statistics, and detection history.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskCreateTypes.h"
#include "util/dto/TaskStatusDto.h"

namespace cosmo::util {
struct DurationStatInfo;
}  // namespace cosmo::util

namespace cosmo {
struct AlgActionDataQueueStatus;
struct DataDetTrackClassify;
struct AlgActionDataQueueStatusDto;
}  // namespace cosmo

namespace cosmo::service {

/// Read-only queries for video analysis task state, including task lists,
/// status, frame info, queue statistics, detection history, and live
/// overview data.
class ITaskQuery {
public:
    virtual ~ITaskQuery() = default;

    /// Get a list of task IDs, optionally filtering by started state.
    /// @param started If true, return only started tasks.
    /// @return Vector of task IDs.
    virtual std::vector<std::string> QueryTasks(bool started = false) = 0;

    /// Get current task parameters.
    /// @param channelId Camera channel identifier.
    /// @param taskId    Task identifier.
    /// @param param     [out] Current parameter configuration.
    /// @return true if the task was found.
    virtual bool GetTaskParam(const std::string& channelId, const std::string& taskId,
                              cosmo::MsgTaskConfig& param) = 0;

    /// Get status information for a list of tasks.
    /// @param tasks       Task IDs to query.
    /// @param durationSec Time window for throughput statistics (default: 30s).
    /// @return Vector of task status DTOs.
    virtual std::vector<cosmo::TaskStatus> GetTaskStatus(const std::vector<std::string>& tasks,
                                                         unsigned int durationSec = 30) = 0;

    /// Get camera info for all currently registered tasks.
    /// @return Vector of camera information structures.
    virtual std::vector<cosmo::MsgCameraInfo> CameraTaskInfo() = 0;

    /// Get frame-level information for a specific task.
    /// @param taskId    Task identifier.
    /// @param bLive     [out] Whether the stream is live.
    /// @param index     [out] Current frame index.
    /// @param pts       [out] Current presentation timestamp.
    /// @param frameSize [out] Frame data size in bytes.
    /// @param streamUrl [out] Source stream URL.
    /// @return true if the task was found.
    virtual bool GetTaskFrameInfo(const std::string& taskId, bool& bLive, int64_t& index, int64_t& pts,
                                  int64_t& frameSize, std::string& streamUrl) = 0;

    /// Get the total number of active tasks.
    virtual size_t TaskCount() = 0;

    /// Get the number of tasks using a specific algorithm.
    /// @param algorithmId Algorithm identifier.
    /// @return Count of tasks using this algorithm.
    virtual int GetAlgorithmCount(const std::string& algorithmId) = 0;

    /// Get action queue status for all tasks (raw flow types).
    /// @param queStatus   [out] Queue status entries.
    /// @param durationSec Time window for statistics (default: 30s).
    virtual void QueueStatus(std::vector<cosmo::AlgActionDataQueueStatus>& queStatus,
                             unsigned int durationSec = 30) = 0;

    /// Get action queue status for all tasks (service-layer DTO types).
    /// Converts from flow-layer AlgActionDataQueueStatus to DTO automatically.
    /// @param queStatus   [out] Queue status DTO entries.
    /// @param durationSec Time window for statistics (default: 30s).
    virtual void QueueStatusDto(std::vector<cosmo::AlgActionDataQueueStatusDto>& queStatus,
                                unsigned int durationSec = 30) = 0;

    /// Get packet processing statistics.
    /// @param total          [out] Total packets received.
    /// @param proc           [out] Packets processed.
    /// @param discard        [out] Packets discarded.
    /// @param discardMaxSec  [out] Maximum discard duration in seconds.
    virtual void PacketStatus(size_t& total, size_t& proc, size_t& discard, size_t& discardMaxSec) = 0;

    /// Get live overview (structured detection) data for a task.
    /// @param taskId      Task identifier.
    /// @param streamIndex Stream index filter (-1 for all).
    /// @param from        Start timestamp filter (-1 for all).
    /// @param to          End timestamp filter (-1 for all).
    /// @return Vector of overview memory entries.
    virtual std::vector<cosmo::MsgOverviewMem> GetTaskLiveOverviewInfo(const std::string& taskId,
                                                                       int64_t streamIndex = -1,
                                                                       int64_t from        = -1,
                                                                       int64_t to          = -1) = 0;

    /// Get camera channel attributes for a given channel.
    /// @param channelId Camera channel identifier.
    /// @param attr      [out] Channel attributes (resolution, FPS, etc.).
    /// @return true if the channel was found.
    virtual bool GetChannelAttr(const std::string& channelId, cosmo::MsgCameraAttr& attr) = 0;

    /// Get detection track history for a channel–task pair.
    /// @param channelId Camera channel identifier.
    /// @param taskId    Task identifier.
    /// @param from      Start timestamp.
    /// @param timestamp Query reference timestamp.
    /// @param to        End timestamp.
    /// @return Vector of detection/track/classify records.
    virtual std::vector<cosmo::DataDetTrackClassify> GetTaskDetHistory(const std::string& channelId,
                                                                       const std::string& taskId,
                                                                       int64_t from, int64_t timestamp,
                                                                       int64_t to) = 0;

    /// Get action processing duration statistics for a task.
    /// @param taskId     Task identifier.
    /// @param durationMs Time window for statistics in milliseconds (default: 5000).
    /// @return Vector of (action name, duration stats) pairs.
    virtual std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>> GetTaskActionDurations(
        const std::string& taskId, int durationMs = 5000) = 0;
};

}  // namespace cosmo::service
