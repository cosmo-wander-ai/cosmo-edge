// Camera service implementation — device CRUD, task configuration proxy,
// image encoding, USB camera enumeration and periodic monitoring.
#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

// Forward declaration — full definition in flow/task/CameraTaskMng.h (included in .cc)
namespace cosmo {
class CameraTaskMng;
using CameraTaskMngPtr = std::shared_ptr<CameraTaskMng>;
}  // namespace cosmo
#include "service/camera/ICameraService.h"
#include "util/Log.h"
#include "util/PeriodicTimer.h"

namespace cosmo::service {

struct CameraEntity {
    std::string videoChannelId;
    std::string channelCode;
    std::string channelName;
    std::string url;
    int channelType{0};
    CameraTaskMngPtr taskMng;

    friend void to_json(nlohmann::json& j, const CameraEntity& v);
    friend void from_json(const nlohmann::json& j, CameraEntity& v);
};
using CameraEntityPtr = std::shared_ptr<CameraEntity>;

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
    void LoadConfig();
    void SaveConfig();
    void MakeTaskMng(CameraEntityPtr camera);
    std::string GetVideoFileName(const std::string& id, const std::string& url);
    void CameraTaskMonitor();
    void MemGc();
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
