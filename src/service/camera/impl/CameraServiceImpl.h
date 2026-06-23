// Camera service implementation — device CRUD, task lifecycle,
// image encoding, USB camera enumeration and periodic monitoring.
// CameraTaskMng logic is inlined directly into CameraServiceImpl.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "service/camera/ICameraService.h"
#include "service/camera/impl/CameraTaskTypes.h"
#include "util/Log.h"
#include "util/PeriodicTimer.h"
#include "util/dto/CameraMsgTypes.h"
#include "util/dto/ChannelStatusDto.h"

namespace cosmo::service {

// Per-camera runtime entity — holds device config and per-camera task state.
// Previously split across CameraEntity (config only) + CameraTaskMng (runtime state).
// Non-copyable, non-movable; always held via shared_ptr.
class CameraEntity {
public:
    CameraEntity() = default;
    ~CameraEntity();

    CameraEntity(const CameraEntity&)            = delete;
    CameraEntity& operator=(const CameraEntity&) = delete;
    CameraEntity& operator=(CameraEntity&&)      = delete;

    // Move constructor — transfers only config fields; runtime state (mutex, atomic, thread)
    // is default-initialized. Only used by nlohmann JSON deserialization during LoadConfig.
    CameraEntity(CameraEntity&& o) noexcept
        : videoChannelId(std::move(o.videoChannelId)),
          channelCode(std::move(o.channelCode)),
          channelName(std::move(o.channelName)),
          url(std::move(o.url)),
          channelType(o.channelType),
          tasks_(std::move(o.tasks_)),
          conf_file_path_(std::move(o.conf_file_path_)),
          conf_task_list_(std::move(o.conf_task_list_)),
          channel_task_(std::move(o.channel_task_)),
          channel_url_(std::move(o.channel_url_)) {}

    // ---- Device config (persisted to JSON) ----
    std::string videoChannelId;
    std::string channelCode;
    std::string channelName;
    std::string url;
    int channelType{0};

    // ---- Per-camera task runtime state (not persisted) ----
    mutable std::shared_mutex task_mtx_;  // Protects tasks_
    std::vector<CameraTaskPtr> tasks_;    // Max 5 algorithm tasks per channel
    size_t max_task_count_{5};

    std::string conf_file_path_{};  // ${cameraCfgPath}/${cameraId}
    std::string conf_task_list_{"taskList.json"};
    std::string channel_task_{};  // channelId + "-ChannelTask"
    std::string channel_url_{};   // Stream URL

    std::atomic<bool> is_capturing_image_{false};
    std::atomic<ChannelStatus> probed_status_{ChannelStatus::ChannelStatusOffline};

    mutable std::mutex attr_mtx_;
    MsgCameraAttr cached_attr_{};  // Cached resolution/codec/fps
    std::mutex switch_mtx_;        // Protects switch_thread_
    std::thread switch_thread_;    // Background switch thread (joinable, replaces detach)

    // Wait for background switch thread to finish
    void WaitForSwitchThread();

    // JSON serialization (only device config fields are serialized)
    friend void to_json(nlohmann::json& j, const CameraEntity& v);
    friend void from_json(const nlohmann::json& j, CameraEntity& v);
};
using CameraEntityPtr = std::shared_ptr<CameraEntity>;

// Custom JSON deserialization for shared_ptr<CameraEntity> (CameraEntity is non-movable)
void from_json(const nlohmann::json& j, CameraEntityPtr& v);

class CameraServiceImpl : public ICameraService {
public:
    CameraServiceImpl();
    ~CameraServiceImpl() override;

    CameraServiceImpl(const CameraServiceImpl&)            = delete;
    CameraServiceImpl& operator=(const CameraServiceImpl&) = delete;

    util::ErrorEnum Add(MsgCameraInfo& config, std::string& id) override;
    util::ErrorEnum Update(MsgCameraInfo& config) override;
    util::ErrorEnum Delete(const std::string& cameraId) override;
    std::vector<MsgCameraInfo> Query(const std::string& channelName, int channelStatus, int pageNum,
                                     int pageSize, size_t& total) override;

    util::ErrorEnum ModifyTaskParam(const std::string& cameraId, const std::string& algorithmId,
                                    MsgTaskConfig& params) override;
    util::ErrorEnum QueryTaskParam(const std::string& cameraId, const std::string& algorithmId,
                                   std::vector<MsgDynamicKeyValue>& params) override;
    util::ErrorEnum ModifyTaskArea(const std::string& cameraId, const std::string& algorithmId,
                                   const std::vector<MsgTaskArea>& areas,
                                   const std::vector<MsgTaskArea>& shieldedAreas = {}) override;
    util::ErrorEnum QueryTaskArea(const std::string& cameraId, const std::string& algorithmId,
                                  std::vector<MsgTaskArea>& areas,
                                  std::vector<MsgTaskArea>& shieldedAreas) override;
    util::ErrorEnum ModifyTaskStrategy(const std::string& cameraId, const std::string& algorithmId,
                                       const std::string& scheduleId) override;
    util::ErrorEnum QueryTaskStrategy(const std::string& cameraId, const std::string& algorithmId,
                                      std::string& scheduleId) override;
    util::ErrorEnum SwitchTask(const std::string& cameraId, const std::string& algorithmId,
                               bool enable) override;
    util::ErrorEnum QuerySwitch(const std::string& cameraId, const std::string& algorithmId,
                                bool& enable) override;
    util::ErrorEnum DeleteTask(const std::string& cameraId, const std::string& algorithmId) override;
    std::vector<service::camera::CameraTaskDto> GetTasks(const std::string& cameraId) override;
    void NotifyAlgorithmsChanged(const std::vector<std::string>& algorithmIds, bool restartRunning) override;
    void NotifyAlgorithmsDeleted(const std::vector<std::string>& algorithmIds) override;
    bool IsAlgorithmInUse(const std::string& algorithmId) const override;

    bool ScheduleInUse(const std::string& scheduleId) override;
    VideoFramePtr CaptureImage(const std::string& channelId, int timeOutMs = 3000) override;
    util::ErrorEnum BindTaskLibPara(const std::string& cameraId, const std::string& algorithmCode,
                                    const std::vector<std::string>& bindLibs,
                                    const std::string& paramKey) override;

    // ---- Image encoding and path utilities ----
    bool IsIotNetworkMode() override;
    std::vector<uint8_t> EncodeJpeg(const VideoFramePtr& frame) override;
    std::string GetWebLocalPath(int64_t timestamp = 0) override;
    std::string GetWebAccessPath(int64_t timestamp = 0) override;

    // ---- USB camera ----
    std::vector<cosmo::camera::MsgUsbCameraDevice> QueryUsbCameraList() override;

    // ---- Testing Dependency Injection ----
    void SetUsbDeviceDirMock(const std::string& dir_path) {
        usb_device_dir_mock_ = dir_path;
    }
    void SetUsbDeviceCheckMock(std::function<bool(const std::string&)> mock_fn) {
        usb_device_check_mock_ = std::move(mock_fn);
    }

    // ---- Channel instance accessors ----
    AlgChannelPtr GetChannelInst(const std::string& channelId) override;
    std::string GetChannelName(const std::string& channelId) const override;
    void InitCameraEntities() override;

private:
    // ---- Config persistence ----
    void LoadConfig();
    void SaveConfig();

    // ---- Camera lookup ----
    std::string GetVideoFileName(const std::string& id, const std::string& url);
    CameraEntityPtr GetCamera(const std::string& cameraId);
    CameraEntityPtr GetCamera(const std::string& cameraId) const;
    template <typename Func>
    util::ErrorEnum WithCamera(const std::string& cameraId, Func&& fn) {
        auto camera = GetCamera(cameraId);
        if (!camera) {
            LOG_INFO("{} Not Exist", cameraId);
            return util::ErrorEnum::CameraNotExist;
        }
        return fn(camera);
    }

    // ---- Per-camera task lifecycle (inlined from CameraTaskMng) ----
    void InitCameraChannel(CameraEntityPtr camera);
    void DestroyCameraChannel(CameraEntityPtr camera);
    void LoadCameraTaskList(CameraEntityPtr camera);
    void SaveCameraTaskList(const CameraEntityPtr& camera);
    util::ErrorEnum MakeCameraTask(const CameraEntityPtr& camera, CameraTaskPtr task);
    void PrepareCameraTaskOverview(const CameraEntityPtr& camera, CameraTaskPtr task);
    void SwitchCameraTask(const CameraEntityPtr& camera, CameraTaskPtr task);
    void SwitchCameraTaskAsync(CameraEntityPtr camera, CameraTaskPtr task);

    // ---- Per-camera monitoring (inlined from CameraTaskMngMonitor) ----
    void CameraTaskMonitor();
    void MonitorCameraEntity(const CameraEntityPtr& camera, bool isAuthed);
    void UpdateChannelState(const CameraEntityPtr& camera);
    void ProbeCameraOnlineStatus(const CameraEntityPtr& camera);
    void ProbeCameraOnlineStatusNow(const CameraEntityPtr& camera);
    void MemGc();

    // ---- Per-camera algorithm notification (inlined from CameraTaskMngNotify) ----
    std::vector<std::string> StopAlgorithmForReload(const CameraEntityPtr& camera,
                                                    const std::string& algorithmCode);
    void RebuildAlgorithmForReload(const CameraEntityPtr& camera, const std::string& algorithmCode);
    void StartTasksAfterReload(const CameraEntityPtr& camera, const std::vector<std::string>& taskIds);
    CameraTaskPtr GetCameraTask(const CameraEntityPtr& camera, const std::string& algorithmCode) const;

private:
    mutable std::shared_mutex mtx_;
    std::string conf_file_path_{"camera"};
    std::string conf_file_name_{"cameraList.json"};

    size_t max_camera_count_{32};
    size_t channel_code_num_{0};
    std::vector<CameraEntityPtr> cameras_;
    std::unique_ptr<PeriodicTimer> timer_;
    TaskId task_monitor_task_id_{kInvalidTaskId};
    TaskId mem_gc_task_id_{kInvalidTaskId};

    std::string usb_device_dir_mock_{"/dev"};
    std::function<bool(const std::string&)> usb_device_check_mock_{nullptr};
};

}  // namespace cosmo::service
