// Task service implementation — lifecycle, orchestration, query,
// channel management, and data recording for video analysis tasks.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

// Forward declarations — full definitions in flow/task/TaskBase.h (included in .cc)
namespace cosmo {
class TaskBase;
class TaskElement;
using TaskElementPtr = std::shared_ptr<TaskElement>;
}  // namespace cosmo

#include "service/task/ITaskService.h"

namespace cosmo::service {

class TaskServiceImpl : public ITaskService {
public:
    TaskServiceImpl();
    ~TaskServiceImpl() override;

    cosmo::util::ErrorEnum TaskCreate(const std::string& channelId, const std::string& channelName,
                                      const std::string& taskId, cosmo::ActionAlgPtr actionAlg) override;
    cosmo::util::ErrorEnum TaskDelete(const std::string& taskId) override;

    cosmo::MsgTaskCreateSend ProcessTaskCreate(cosmo::MsgTaskCreateRecv& data,
                                               std::error_condition& errc) override;
    cosmo::MsgTaskCancleSend ProcessTaskCancel(cosmo::MsgTaskCancleRecv& data,
                                               std::error_condition& errc) override;

    bool TaskStart(const std::string& channelId, const std::string& taskId) override;
    bool TaskStop(const std::string& taskId) override;
    bool TaskIsStart(const std::string& taskId) override;

    void TaskChannelSetUrl(const std::string& channelId, const std::string& url) override;
    void TaskChannelSetParam(const std::string& channelId, const std::string& url,
                             int videoRepeatCount) override;

    VideoFramePtr CaptureImage(const std::string& channelId, int timeOutMs = 3000) override;

    std::vector<std::string> QueryTasks(bool started = false) override;
    bool SetTaskParam(const std::string& channelId, const std::string& taskId,
                      cosmo::MsgTaskConfig& param) override;
    bool GetTaskParam(const std::string& channelId, const std::string& taskId,
                      cosmo::MsgTaskConfig& param) override;
    std::vector<cosmo::TaskStatus> GetTaskStatus(const std::vector<std::string>& tasks,
                                                 unsigned int durationSec = 30) override;
    std::vector<cosmo::MsgCameraInfo> CameraTaskInfo() override;

    bool GetTaskFrameInfo(const std::string& taskId, bool& bLive, int64_t& index, int64_t& pts,
                          int64_t& frameSize, std::string& streamUrl) override;
    bool GetChannelAttr(const std::string& channelId, cosmo::MsgCameraAttr& attr) override;
    bool TaskDataActive(const std::string& channelId) override;
    size_t TaskCount() override;
    int GetAlgorithmCount(const std::string& algorithmId) override;

    void QueueStatus(std::vector<cosmo::AlgActionDataQueueStatus>& queStatus,
                     unsigned int durationSec = 30) override;
    void QueueStatusDto(std::vector<cosmo::AlgActionDataQueueStatusDto>& queStatus,
                        unsigned int durationSec = 30) override;
    void PacketStatus(size_t& total, size_t& proc, size_t& discard, size_t& discardMaxSec) override;

    std::vector<cosmo::MsgOverviewMem> GetTaskLiveOverviewInfo(const std::string& taskId,
                                                               int64_t streamIndex = -1, int64_t from = -1,
                                                               int64_t to = -1) override;

    std::vector<cosmo::DataDetTrackClassify> GetTaskDetHistory(const std::string& channelId,
                                                               const std::string& taskId, int64_t from,
                                                               int64_t timestamp, int64_t to) override;

    bool LogicTest(const std::string& taskId, cosmo::MsgTarget& target) override;

    cosmo::AlgChannelPtr GetChannelInst(const std::string& channelId) override;
    std::vector<std::string> GetChannelTasks(const std::string& channelId) override;
    cosmo::TaskAlarmPtr GetAlarmInst(const std::string& channelId, const std::string& taskId) override;
    std::string GetTaskChannel(const std::string& taskId) override;

    void GetCameraInfo(std::vector<cosmo::MsgCameraInfo>& cameraInfos) override;

    std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>> GetTaskActionDurations(
        const std::string& taskId, int durationMs = 5000) override;

    void ShowActions(cosmo::ActionAlgPtr actionAlg) override;

    void RecordClearTaskData(const std::string& taskId) override;
    void RecordTaskInfo(const std::string& taskId, cosmo::MsgTaskCreateRecv& data) override;
    void RecordTaskAction(const std::string& taskId, cosmo::ActionAlgPtr data) override;

    // Non-interface methods (migrated from TaskMng)
    void ActionInfo(std::vector<cosmo::ActionRuntimeInfo>& actionInfo);
    void TaskChannelRepeatCount(const std::string& channelId, int videoRepeatCount);
    void AddTaskChannel(const cosmo::MsgTaskCreateRecv& taskCreateRecv);
    void DeleteTaskChannel(const cosmo::MsgTaskCancleRecv& taskCancleRecv);

private:
    std::string GetTaskAlgId(const std::string& taskId);

    std::shared_mutex mtx_;
    std::unique_ptr<cosmo::TaskBase> task_base_;
    std::map<std::string, cosmo::TaskElementPtr> tasks_;

    // Log rate-limiting state (migrated from GetTaskLiveOverviewInfo static local)
    std::mutex log_throttle_mtx_;
    std::unordered_map<std::string, int64_t> not_in_pool_log_ts_;
    int64_t last_overview_log_ts_{0};
};

}  // namespace cosmo::service
