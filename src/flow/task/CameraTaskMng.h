// Camera task manager — owns per-camera algorithm task lifecycle.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>

#include "flow/task/CameraTaskUnit.h"
#include "media/VideoFrame.h"
#include "util/MsgBaseTypes.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/CameraTaskDto.h"
#include "util/dto/CosmoFwd.h"
#include "util/dto/FilterTypes.h"
#include "util/dto/OverviewTypes.h"
#include "util/dto/TaskCreateTypes.h"

namespace cosmo {

enum class CameraTaskOpStatus {
    kNone = 0,
    kOpenByConfig,      // Opened by config
    kOpenByInterface,   // Opened by API
    kOpenBySchedule,    // Opened by schedule template
    kOpenByAuth,        // Opened by authorization
    kCloseByConfig,     // Closed by config
    kCloseByInterface,  // Closed by API
    kCloseBySchedule,   // Closed by schedule template
    kCloseByAuth        // Closed by authorization
};
enum class CameraTaskStatus {
    kPause = -1,
    kStop,       // Stopped
    kInService,  // Running
    kAbnormal    // Abnormal
};
struct CameraTaskOp {
    int64_t op_timestamp_{0};   // Operation timestamp (not persisted)
    CameraTaskOpStatus status;  // Operation status (not persisted)
};

struct CameraTask {
    std::string task_id_{};
    std::string algorithm_code_{};  // Algorithm code
    std::string algorithm_name_{};  // Algorithm name
    std::string schedule_id_{};     // Schedule template ID
    std::string schedule_name_{};   // Schedule template name
    bool is_enabled_{false};        // Enable switch
    CameraTaskStatus status_{CameraTaskStatus::kStop};
    std::vector<CameraTaskOp> ops_;  // Operation log (max 10 entries, not persisted)

    MsgTaskCreateRecv data_;   // Structured overlay data
    ActionAlgPtr action_alg_;  // Algorithm orchestration config for overlay
    CameraTaskUnitPtr task_;
};

void to_json(nlohmann::json& j, const CameraTask& v);
void from_json(const nlohmann::json& j, CameraTask& v);
using CameraTaskPtr = std::shared_ptr<CameraTask>;

class CameraTaskMng {
public:
    CameraTaskMng(const std::string& cameraCfgPath, const std::string& cameraId,
                  const std::string& channelUrl);
    ~CameraTaskMng();

    void SetChannelUrl(const std::string& channelUrl);

    // Get probed online status (used when stream is not started)
    ChannelStatus GetProbedStatus() const;

    // Get cached video attributes (resolution, codec, fps); returns last cached value when channel is idle
    MsgCameraAttr GetCachedAttr() const;

    util::ErrorEnum SetArea(const std::string& algorithmCode, const std::vector<MsgTaskArea>& areas,
                            const std::vector<MsgTaskArea>& shieldedAreas = {});
    util::ErrorEnum GetArea(const std::string& algorithmCode, std::vector<MsgTaskArea>& areas,
                            std::vector<MsgTaskArea>& shieldedAreas);
    util::ErrorEnum SetParams(const std::string& algorithmCode, MsgTaskConfig& params);
    util::ErrorEnum GetParams(const std::string& algorithmCode, std::vector<MsgDynamicKeyValue>& params);
    util::ErrorEnum SetStrategySchedule(const std::string& algorithmCode, const std::string& scheduleId);
    util::ErrorEnum GetStrategySchedule(const std::string& algorithmCode, std::string& scheduleId);
    util::ErrorEnum Switch(const std::string& algorithmCode, bool enable);
    util::ErrorEnum GetSwitch(const std::string& algorithmCode, bool& enable);

    [[nodiscard]] std::vector<service::camera::CameraTaskDto> GetTasks() const;
    void NotifyAlgorithmChanged(const std::string& algorithmCode, bool restartRunning);
    void NotifyAlgorithmDeleted(const std::string& algorithmCode);
    std::vector<std::string> StopAlgorithmForReload(const std::string& algorithmCode);
    void RebuildAlgorithmForReload(const std::string& algorithmCode);
    void StartTasksAfterReload(const std::vector<std::string>& taskIds);

    [[nodiscard]] CameraTaskPtr GetCameraTask(const std::string& algorithmCode) const;

    VideoFramePtr CaptureImage(int timeOutMs);

    // Check whether a schedule template is currently in use
    [[nodiscard]] bool ScheduleInUse(const std::string& scheduleId) const;

    [[nodiscard]] std::vector<MsgCameraTask> Query(int pageNum, int pageSize, size_t& total) const;

    void Monitor(bool isAuthed);  // Monitor loop: start/stop tasks based on schedule and auth

    util::ErrorEnum DeleteTask(const std::string& algorithmCode);

    void ProbeOnlineStatusNow();

private:
    void SaveTaskList();
    void LoadTaskList();

    util::ErrorEnum MakeTask(CameraTaskPtr task);

    void MakeTaskOverviewParam(CameraTaskPtr task);

    void SwitchTask(CameraTaskPtr task);
    void SwitchTaskAsync(CameraTaskPtr task);  // Execute in background thread to avoid blocking HTTP handler
    void WaitForSwitchThread();                // Wait for background switch thread to finish

    void UpdateChannelTaskState();
    void ProbeOnlineStatus();

private:
    mutable std::shared_mutex mtx_;
    std::string conf_file_path_{};  // ${cameraCfgPath}/${cameraId}
    std::string conf_task_list_{"taskList.json"};
    std::string channel_id_{};
    std::string channel_task_{};
    std::string channel_url_{};
    std::vector<CameraTaskPtr> tasks_;  // Max 5 tasks per channel
    size_t camera_max_task_count_{5};

    std::atomic<bool> is_capturing_image_{false};
    std::atomic<ChannelStatus> probed_status_{ChannelStatus::ChannelStatusOffline};

    mutable std::mutex attr_mtx_;
    MsgCameraAttr cached_attr_{};  // Cached resolution/codec/fps
    std::thread switch_thread_;    // Background switch thread (joinable, replaces detach)
};

using CameraTaskMngPtr = std::shared_ptr<CameraTaskMng>;
}  // namespace cosmo
