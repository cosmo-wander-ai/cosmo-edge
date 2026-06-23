// CameraServiceImpl.cc — Core lifecycle, config persistence, monitoring, USB camera and utilities.
// CameraTaskMng logic is inlined directly — no more middleman class.
// Device CRUD operations are in CameraDeviceCrud.cc.
// Task configuration operations are in CameraTaskConfig.cc.

#include "service/camera/impl/CameraServiceImpl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <malloc.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <regex>

#include "flow/channel/AlgChannel.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/common/FlowTaskUtil.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/camera/impl/CameraConfigPersistence.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/model/IModelQuery.h"
#include "service/model/IModelService.h"
#include "service/system/IConfigReadService.h"
#include "service/task/IScheduleService.h"
#include "service/task/ITaskChannel.h"
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskQuery.h"
#include "util/FileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"
#include "util/PaginationHelper.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/dto/ChannelStatusDto.h"

namespace cosmo::service {

// ============================================================
//  CameraEntity lifecycle
// ============================================================

CameraEntity::~CameraEntity() {
    WaitForSwitchThread();
}

void CameraEntity::WaitForSwitchThread() {
    std::lock_guard<std::mutex> lock(switch_mtx_);
    if (switch_thread_.joinable()) {
        switch_thread_.join();
    }
}

// ============================================================
//  Anonymous helpers
// ============================================================

namespace {
    bool IsUsableUsbCameraDevice(const std::string& device_path) {
        int fd = ::open(device_path.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (fd < 0) {
            LOG_INFO("USB camera filter skip {}: open failed, errno={}", device_path, errno);
            return false;
        }
        // RAII guard: ensures fd is closed on all exit paths
        auto fd_guard = std::unique_ptr<int, void (*)(int*)>{&fd, [](int* p) { ::close(*p); }};

        v4l2_capability cap{};
        if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            LOG_INFO("USB camera filter skip {}: VIDIOC_QUERYCAP failed, errno={}", device_path, errno);
            return false;
        }

        const std::string driver   = reinterpret_cast<const char*>(cap.driver);
        const std::string card     = reinterpret_cast<const char*>(cap.card);
        const std::string bus_info = reinterpret_cast<const char*>(cap.bus_info);

        LOG_INFO("USB camera filter check {}: driver={} card={} bus={} caps=0x{:x} device_caps=0x{:x}",
                 device_path, driver, card, bus_info, static_cast<unsigned int>(cap.capabilities),
                 static_cast<unsigned int>(cap.device_caps));

        // Only keep video devices on USB bus; avoid misidentifying virtual/non-USB devices
        if (bus_info.empty() || bus_info.rfind("usb-", 0) != 0) {
            LOG_INFO("USB camera filter skip {}: non-usb bus_info='{}'", device_path, bus_info);
            return false;
        }

        // Per V4L2 semantics, select the correct capability bits:
        // If capabilities contains DEVICE_CAPS, use device_caps for the current node.
        const bool has_device_caps    = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0;
        const uint32_t effective_caps = has_device_caps ? static_cast<uint32_t>(cap.device_caps)
                                                        : static_cast<uint32_t>(cap.capabilities);

        // Only keep nodes that can truly capture video: must include VIDEO_CAPTURE(0x00000001)
        const bool has_video_capture = (effective_caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
        if (!has_video_capture) {
            LOG_INFO(
                "USB camera filter skip {}: no VIDEO_CAPTURE in effective caps, "
                "has_device_caps={} effective_caps=0x{:x}",
                device_path, has_device_caps ? 1 : 0, effective_caps);
            return false;
        }

        const bool has_io_capability =
            ((effective_caps & V4L2_CAP_STREAMING) != 0) || ((effective_caps & V4L2_CAP_READWRITE) != 0);
        if (!has_io_capability) {
            LOG_INFO("USB camera filter skip {}: no io capability (STREAMING/READWRITE)", device_path);
            return false;
        }

        LOG_INFO(
            "USB camera filter pass {}: driver={} card={} bus={} has_device_caps={} effective_caps=0x{:x}",
            device_path, driver, card, bus_info, has_device_caps ? 1 : 0, effective_caps);
        return true;
    }

    // Lightweight URL connectivity check (TCP socket probe).
    bool CheckUrlConnectivity(const std::string& urlStr) {
        if (urlStr.empty())
            return false;

        if (urlStr.find("usb://") == 0) {
            auto startPos        = 6;
            auto questionMarkPos = urlStr.find("?");
            std::string indexStr;
            if (questionMarkPos != std::string::npos) {
                indexStr = urlStr.substr(startPos, questionMarkPos - startPos);
            } else {
                indexStr = urlStr.substr(startPos);
            }
            std::string devPath = "/dev/video" + indexStr;
            if (::access(devPath.c_str(), F_OK) == 0) {
                return true;
            }
            return false;
        }

        // For local files or dev nodes
        if (urlStr.find("rtsp://") == std::string::npos && urlStr.find("http://") == std::string::npos &&
            urlStr.find("https://") == std::string::npos) {
            if (::access(urlStr.c_str(), F_OK) == 0) {
                return true;
            }
            return false;
        }

        // Simplistic port check for RTSP/HTTP (fallback to format parsing)
        std::string ip;
        int port = -1;

        auto protoPos = urlStr.find("://");
        if (protoPos != std::string::npos) {
            std::string withoutProto = urlStr.substr(protoPos + 3);
            // Strip out auth part user:pass@
            auto atPos = withoutProto.find("@");
            if (atPos != std::string::npos) {
                withoutProto = withoutProto.substr(atPos + 1);
            }
            // Extract IP and Port
            auto slashPos        = withoutProto.find("/");
            std::string hostPort = withoutProto.substr(0, slashPos);

            auto colonPos = hostPort.find(":");
            if (colonPos != std::string::npos) {
                ip = hostPort.substr(0, colonPos);
                try {
                    port = std::stoi(hostPort.substr(colonPos + 1));
                } catch (const std::exception& e) {
                    LOG_WARN("Failed to parse port from '{}': {}", hostPort, e.what());
                }
            } else {
                ip = hostPort;
                if (urlStr.find("rtsp://") == 0)
                    port = 554;
                else if (urlStr.find("https://") == 0)
                    port = 443;
                else if (urlStr.find("http://") == 0)
                    port = 80;
            }
        }

        if (ip.empty() || port <= 0)
            return false;

        // RAII wrapper: ensures socket fd is closed on all exit paths
        struct ScopedSocket {
            int fd;
            explicit ScopedSocket(int f) : fd(f) {}
            ~ScopedSocket() {
                if (fd >= 0)
                    ::close(fd);
            }
            ScopedSocket(const ScopedSocket&)            = delete;
            ScopedSocket& operator=(const ScopedSocket&) = delete;
        };

        ScopedSocket sock(socket(AF_INET, SOCK_STREAM, 0));
        if (sock.fd < 0)
            return false;

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port   = htons(port);

        if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            return false;
        }

        // Set non-blocking
        int flags = fcntl(sock.fd, F_GETFL, 0);
        fcntl(sock.fd, F_SETFL, flags | O_NONBLOCK);

        int res = connect(sock.fd, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr));
        if (res == 0) {
            return true;
        }
        if (res < 0 && errno == EINPROGRESS) {
            struct timeval tv;
            tv.tv_sec  = 2;  // 2 sec timeout
            tv.tv_usec = 0;
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(sock.fd, &fdset);

            res = select(sock.fd + 1, nullptr, &fdset, nullptr, &tv);
            if (res > 0 && FD_ISSET(sock.fd, &fdset)) {
                int lon;
                socklen_t lonLen = sizeof(int);
                getsockopt(sock.fd, SOL_SOCKET, SO_ERROR, static_cast<void*>(&lon), &lonLen);
                if (lon == 0) {
                    return true;
                }
            }
        }
        return false;
    }

}  // namespace

// ============================================================
//  Construction / Destruction
// ============================================================

CameraServiceImpl::CameraServiceImpl() {
    LOG_INFO("{}", "CameraServiceImpl created (deferred init)");
}

CameraServiceImpl::~CameraServiceImpl() {
    if (task_monitor_task_id_ != kInvalidTaskId) {
        timer_->Cancel(task_monitor_task_id_);
        task_monitor_task_id_ = kInvalidTaskId;
    }
    if (mem_gc_task_id_ != kInvalidTaskId) {
        timer_->Cancel(mem_gc_task_id_);
        mem_gc_task_id_ = kInvalidTaskId;
    }
    if (timer_) {
        timer_->Destroy();
    }
    for (auto& camera : cameras_) {
        if (camera) {
            camera->WaitForSwitchThread();
        }
    }
    LOG_INFO("{}", "CameraServiceImpl Delete");
}

// ============================================================
//  Per-camera channel lifecycle (inlined from CameraTaskMng ctor/dtor)
// ============================================================

void CameraServiceImpl::InitCameraChannel(CameraEntityPtr camera) {
    camera->conf_file_path_ =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / camera->videoChannelId).string();
    camera->channel_task_ = camera->videoChannelId + "-ChannelTask";
    camera->channel_url_  = camera->url;

    LoadCameraTaskList(camera);

    ActionAlgPtr action_alg   = std::make_shared<ActionAlg>();
    action_alg->algorithmCode = "Channel";
    // Create a channel task for frame capture (RTSP streams or local video files).
    // Actual Start is deferred until an analysis task is enabled, saving NPU and bandwidth.
    ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskCreate(
        camera->videoChannelId, camera->videoChannelId, camera->channel_task_, action_alg);
    ServiceRegistry::Instance().Get<ITaskChannel>().TaskChannelSetUrl(camera->videoChannelId,
                                                                      camera->channel_url_);
    LOG_INFO("[{}] CameraChannel Init", camera->videoChannelId);
}

void CameraServiceImpl::DestroyCameraChannel(CameraEntityPtr camera) {
    // 1. Wait for async switch thread to complete, preventing use-after-free
    camera->WaitForSwitchThread();

    // 2. Explicitly stop/delete all algorithm tasks (do not rely on implicit vector destruction).
    //    Must be done before channel task deletion to ensure AlgChannel refcount decrements correctly.
    {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
        for (auto& task : camera->tasks_) {
            if (task && task->task_) {
                LOG_INFO("[{}] DestroyCameraChannel: Destroying algo task {}", camera->videoChannelId,
                         task->algorithm_code_);
                task->task_.reset();
            }
        }
        camera->tasks_.clear();
    }

    // 3. Finally stop/delete the channel task (all algorithm tasks are cleaned up)
    ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(camera->channel_task_);
    ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskDelete(camera->channel_task_);
    LOG_INFO("[{}] CameraChannel Delete", camera->videoChannelId);
}

// ============================================================
//  Per-camera task list persistence (inlined from CameraTaskMng)
// ============================================================

void CameraServiceImpl::SaveCameraTaskList(const CameraEntityPtr& camera) {
    auto path =
        (std::filesystem::path(cosmo::path::GetCfgPath(camera->conf_file_path_)) / camera->conf_task_list_)
            .string();
    (void)util::SaveStructToJsonFile(path, camera->tasks_);
}

void CameraServiceImpl::LoadCameraTaskList(CameraEntityPtr camera) {
    auto cfgPath =
        (std::filesystem::path(cosmo::path::GetCfgPath(camera->conf_file_path_)) / camera->conf_task_list_)
            .string();
    (void)util::LoadStructFromJsonFile(cfgPath, camera->tasks_);
    for (auto it = camera->tasks_.begin(); it != camera->tasks_.end();) {
        auto alg_temp = MakeCameraTask(camera, *it);
        if (util::ErrorEnum::Success != alg_temp) {
            auto algCode = (*it)->algorithm_code_;
            it           = camera->tasks_.erase(it);
            LOG_WARN("{} Make Task failed", algCode);
        } else {
            it++;
        }
    }
}

// ============================================================
//  Per-camera task creation and switching (inlined from CameraTaskMng)
// ============================================================

util::ErrorEnum CameraServiceImpl::MakeCameraTask(const CameraEntityPtr& camera, CameraTaskPtr task) {
    if (camera->tasks_.size() >= camera->max_task_count_) {
        return util::ErrorEnum::TaskTooMuch;
    }
    auto algData = ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithm(task->algorithm_code_);
    if (!algData) {
        return util::ErrorEnum::ActionAlgNotExist;
    }

    task->action_alg_ = algData;

    std::vector<ModelInfo> models;
    for (const auto& workFlow : algData->workFlow) {
        if (!workFlow.atomicCode.empty()) {
            LOG_INFO("[{}/{}] [{}/{}]", workFlow.actionId, workFlow.actionName, workFlow.atomicCode,
                     workFlow.atomAlgName);
            auto modelInfo =
                ServiceRegistry::Instance().Get<IModelService>().GetModelInfo(workFlow.atomicCode);
            if (modelInfo.id == workFlow.atomicCode) {
                models.push_back(modelInfo);
            }
        }
    }

    if (task->schedule_id_.empty()) {
        task->schedule_id_ = ServiceRegistry::Instance().Get<IScheduleService>().GetDefaultId();
    }

    // set taskId
    task->task_id_        = ChannelAlgIdToTaskId(camera->videoChannelId, task->algorithm_code_);
    task->algorithm_name_ = algData->algorithmName;
    auto taskUnit         = std::make_shared<CameraTaskUnit>(camera->conf_file_path_, camera->videoChannelId,
                                                     task->algorithm_code_, models);
    if (!taskUnit->IsReady()) {
        auto status = taskUnit->GetStatus();
        LOG_WARN("[{}/{}] Make Task failed, unit status:{}", camera->videoChannelId, task->algorithm_code_,
                 static_cast<uint32_t>(status));
        return status;
    }
    task->task_ = std::move(taskUnit);
    LOG_INFO("[{}/{}] Make Task:{}", camera->videoChannelId, task->algorithm_code_, task->task_id_);
    return util::ErrorEnum::Success;
}

void CameraServiceImpl::PrepareCameraTaskOverview(const CameraEntityPtr& camera, CameraTaskPtr task) {
    if (!task || !task->task_ || !task->task_->IsReady()) {
        LOG_WARN("[{}] PrepareCameraTaskOverview skipped because task unit is not ready",
                 task ? task->task_id_ : "");
        return;
    }
    RecordAlgDataClearTaskData(task->task_id_);
    task->task_->GetArea(task->data_.taskConfig.areas, task->data_.taskConfig.shieldedAreas);
    task->data_.taskConfig.params = task->task_->GetParams();
    task->data_.streamUrl         = camera->channel_url_;
    RecordAlgTaskInfo(task->task_id_, task->data_);
    RecordAlgTaskAction(task->task_id_, task->action_alg_);
}

void CameraServiceImpl::SwitchCameraTask(const CameraEntityPtr& camera, CameraTaskPtr task) {
    if (!task) {
        LOG_WARN("[{}] SwitchCameraTask skipped because task is null", camera->videoChannelId);
        return;
    }
    if (!task->task_ || !task->task_->IsReady()) {
        LOG_WARN("[{}/{}] SwitchCameraTask skipped because task unit is not ready", camera->videoChannelId,
                 task->task_id_);
        task->status_ = CameraTaskStatus::kAbnormal;
        return;
    }
    if (task->is_enabled_) {
        PrepareCameraTaskOverview(camera, task);

        if (ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStart(camera->videoChannelId,
                                                                        task->task_id_)) {
            task->status_ = CameraTaskStatus::kInService;
        } else {
            task->status_ = CameraTaskStatus::kAbnormal;
        }
    } else {
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(task->task_id_);
        task->status_ = CameraTaskStatus::kStop;
    }
    UpdateChannelState(camera);
}

void CameraServiceImpl::SwitchCameraTaskAsync(CameraEntityPtr camera, CameraTaskPtr task) {
    std::lock_guard<std::mutex> lock(camera->switch_mtx_);
    if (camera->switch_thread_.joinable()) {
        camera->switch_thread_.join();
    }

    std::string channelId = camera->videoChannelId;
    camera->switch_thread_ =
        std::thread([this, camera, task, channelId]() {
            LOG_INFO("[{}/{}] SwitchCameraTaskAsync START in background thread", channelId, task->task_id_);
            SwitchCameraTask(camera, task);
            LOG_INFO("[{}/{}] SwitchCameraTaskAsync DONE", channelId, task->task_id_);
        });
}

// ============================================================
//  Per-camera monitoring (inlined from CameraTaskMngMonitor)
// ============================================================

void CameraServiceImpl::CameraTaskMonitor() {
    // Authorization removed: no longer check auth; always treat as authorized (matches old 23461e05)
    const bool is_service_authed = true;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& camera : cameras_) {
        MonitorCameraEntity(camera, is_service_authed);
    }
}

void CameraServiceImpl::MonitorCameraEntity(const CameraEntityPtr& camera, bool isAuthed) {
    std::vector<CameraTaskPtr> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        snapshot.assign(camera->tasks_.begin(), camera->tasks_.end());
    }
    for (auto& task : snapshot) {
        if (!task) {
            LOG_WARN("[{}] Monitor skipped null task", camera->videoChannelId);
            continue;
        }
        if (!task->task_ || !task->task_->IsReady()) {
            LOG_WARN("[{}/{}] Monitor skipped task because task unit is not ready", camera->videoChannelId,
                     task->task_id_);
            task->status_ = CameraTaskStatus::kAbnormal;
            continue;
        }
        task->task_->TaskEnableParam();
        bool taskRunningStatus =
            ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(task->task_id_);
        // Task is currently running
        if (taskRunningStatus) {
            // Stop if task is disabled, outside schedule window, or unauthorized
            if ((!task->is_enabled_) ||
                (!ServiceRegistry::Instance().Get<IScheduleService>().InRunTime(task->schedule_id_) ||
                 (!isAuthed))) {
                LOG_INFO("[{}/{}] Stop TaskEnable:{} AUTH:{} Schedule:{}/{}", camera->videoChannelId,
                         task->task_id_, task->is_enabled_, isAuthed, task->schedule_id_,
                         task->schedule_name_);
                ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(task->task_id_);
                if (task->is_enabled_)                         // Switch is still on
                    task->status_ = CameraTaskStatus::kPause;  // Paused
                else
                    task->status_ = CameraTaskStatus::kStop;  // Stopped
            }
            // Auto-stop task and release GPU memory when offline/VOD video finishes.
            else {
                MsgCameraAttr attr;
                if (ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelAttr(camera->videoChannelId,
                                                                                   attr)) {
                    bool isReadEnd =
                        (attr.dataStatus == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxReadEnd));
                    // Channel has finished reading and no active data remains (queue fully consumed)
                    if (isReadEnd && !ServiceRegistry::Instance().Get<ITaskChannel>().TaskDataActive(
                                         camera->videoChannelId)) {
                        LOG_INFO("[{}/{}] Offline video completed, auto-stopping task to release resources",
                                 camera->videoChannelId, task->task_id_);
                        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(task->task_id_);
                        task->is_enabled_ = false;
                        task->status_     = CameraTaskStatus::kStop;
                        {
                            std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
                            SaveCameraTaskList(camera);
                        }
                    }
                }
            }
        }
        // Task is not running
        else {
            if ((task->is_enabled_) &&
                (ServiceRegistry::Instance().Get<IScheduleService>().InRunTime(task->schedule_id_) &&
                 (isAuthed))) {
                LOG_INFO("[{}/{}] Start", camera->videoChannelId, task->task_id_);

                PrepareCameraTaskOverview(camera, task);

                if (ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStart(camera->videoChannelId,
                                                                                task->task_id_)) {
                    task->status_ = CameraTaskStatus::kInService;
                } else {
                    task->status_ = CameraTaskStatus::kAbnormal;
                }
            }
        }
    }
    UpdateChannelState(camera);
    ProbeCameraOnlineStatus(camera);
}

void CameraServiceImpl::UpdateChannelState(const CameraEntityPtr& camera) {
    bool anyTaskRunning = false;
    std::vector<CameraTaskPtr> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        snapshot.assign(camera->tasks_.begin(), camera->tasks_.end());
    }
    for (auto& task : snapshot) {
        if (ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(task->task_id_)) {
            anyTaskRunning = true;
            break;
        }
    }

    bool channelRunning =
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(camera->channel_task_);
    if (anyTaskRunning && !channelRunning) {
        LOG_INFO("[{}] Auto-starting ChannelTask because active analysis tasks exist",
                 camera->videoChannelId);
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStart(camera->videoChannelId,
                                                                    camera->channel_task_);
    } else if (!anyTaskRunning && channelRunning && !camera->is_capturing_image_.load()) {
        // Cache video attributes before stopping the channel
        MsgCameraAttr attr;
        if (ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelAttr(camera->videoChannelId, attr)) {
            if (attr.width > 0 && attr.height > 0) {
                std::lock_guard<std::mutex> lock(camera->attr_mtx_);
                camera->cached_attr_ = attr;
            }
        }
        LOG_INFO("[{}] Auto-stopping ChannelTask because no active analysis tasks exist",
                 camera->videoChannelId);
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(camera->channel_task_);
        auto channel = ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelInst(camera->videoChannelId);
        if (channel)
            channel->Quit();
        camera->probed_status_.store(ChannelStatus::ChannelStatusOffline);
    }
}

void CameraServiceImpl::ProbeCameraOnlineStatus(const CameraEntityPtr& camera) {
    // Only probe if the channel is actually stopped to save overhead when already active
    if (ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(camera->channel_task_)) {
        return;
    }

    bool isConnected = CheckUrlConnectivity(camera->channel_url_);
    camera->probed_status_.store(isConnected ? ChannelStatus::ChannelStatusOnline
                                             : ChannelStatus::ChannelStatusOffline);
}

void CameraServiceImpl::ProbeCameraOnlineStatusNow(const CameraEntityPtr& camera) {
    bool isConnected = CheckUrlConnectivity(camera->channel_url_);
    camera->probed_status_.store(isConnected ? ChannelStatus::ChannelStatusOnline
                                             : ChannelStatus::ChannelStatusOffline);
}

// ============================================================
//  Private helper methods — config persistence and lookup
// ============================================================

void CameraServiceImpl::LoadConfig() {
    cameras_       = detail::CameraConfigPersistence::LoadConfig(conf_file_path_, conf_file_name_);
    int max_number = -1;
    for (const auto& camera : cameras_) {
        LOG_INFO("LoadConfig channel Id {}", camera->videoChannelId);
        InitCameraChannel(camera);
        int current_number = -1;
        detail::CameraConfigPersistence::ExtractCameraNumber(camera->videoChannelId, &current_number);
        if (current_number > max_number) {
            max_number = current_number;
        }
    }
    channel_code_num_ = max_number;
    LOG_INFO("LoadConfig success and max number :{}", channel_code_num_);

    detail::CameraConfigPersistence::RemoveDiscardedConfigs(conf_file_path_, cameras_);
}

void CameraServiceImpl::SaveConfig() {
    LOG_INFO("{}", "Saving configuration...");

    std::vector<CameraEntityPtr> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        snapshot = cameras_;
    }

    detail::CameraConfigPersistence::SaveConfig(conf_file_path_, conf_file_name_, snapshot);
}

void CameraServiceImpl::MemGc() {
    LOG_INFO("{}", "malloctrim Start");
    malloc_trim(0);
    LOG_INFO("{}", "malloctrim End");
}

CameraEntityPtr CameraServiceImpl::GetCamera(const std::string& cameraId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(cameras_.begin(), cameras_.end(),
                           [&](const CameraEntityPtr& cfg) { return cfg->videoChannelId == cameraId; });

    return (it != cameras_.end()) ? *it : nullptr;
}

CameraEntityPtr CameraServiceImpl::GetCamera(const std::string& cameraId) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(cameras_.begin(), cameras_.end(),
                           [&](const CameraEntityPtr& cfg) { return cfg->videoChannelId == cameraId; });

    return (it != cameras_.end()) ? *it : nullptr;
}

std::string CameraServiceImpl::GetVideoFileName(const std::string& id, const std::string& url) {
    return (std::filesystem::path(cosmo::path::GetCameraPath()) / (id + url.substr(url.find_last_of('.'))))
        .string();
}

// ============================================================
//  Image encoding and path utilities
// ============================================================

bool CameraServiceImpl::IsIotNetworkMode() {
    return RunMode::RunModeIotNetwork == ServiceRegistry::Instance().Get<IConfigReadService>().GetRunMode();
}

std::vector<uint8_t> CameraServiceImpl::EncodeJpeg(const VideoFramePtr& frame) {
    return ServiceRegistry::Instance().Get<IVideoFrameCodec>().EncodeJpeg(frame);
}

std::string CameraServiceImpl::GetWebLocalPath(int64_t timestamp) {
    if (timestamp > 0) {
        return cosmo::path::GetWebLocalPath(timestamp);
    }
    return cosmo::path::GetWebLocalPath();
}

std::string CameraServiceImpl::GetWebAccessPath(int64_t timestamp) {
    if (timestamp > 0) {
        return cosmo::path::GetWebAcessPath(timestamp);
    }
    return cosmo::path::GetWebAcessPath();
}

// ============================================================
//  USB camera
// ============================================================

std::vector<cosmo::camera::MsgUsbCameraDevice> CameraServiceImpl::QueryUsbCameraList() {
    namespace fs = std::filesystem;
    std::vector<cosmo::camera::MsgUsbCameraDevice> usb_devices;
    std::regex video_regex("^video([0-9]+)$");

    try {
        const fs::path devDir(usb_device_dir_mock_);
        if (!fs::exists(devDir) || !fs::is_directory(devDir)) {
            return usb_devices;
        }

        const bool is_mock_dir = (usb_device_dir_mock_ != "/dev");
        for (const auto& entry : fs::directory_iterator(devDir)) {
            // In production (/dev), only character devices are valid.
            // In test (mock dir), skip this check since test files are regular files.
            if (!is_mock_dir && !entry.is_character_file()) {
                continue;
            }

            const std::string file_name = entry.path().filename().string();
            std::smatch match;
            if (!std::regex_match(file_name, match, video_regex)) {
                continue;
            }

            int index = -1;
            try {
                index = std::stoi(match[1].str());
            } catch (const std::exception&) {
                continue;
            }
            if (index < 0) {
                continue;
            }

            cosmo::camera::MsgUsbCameraDevice item;
            item.usbDeviceIndex = index;
            item.devicePath     = usb_device_dir_mock_ + "/" + file_name;

            bool is_usable = false;
            if (usb_device_check_mock_) {
                is_usable = usb_device_check_mock_(item.devicePath);
            } else {
                is_usable = IsUsableUsbCameraDevice(item.devicePath);
            }
            if (!is_usable) {
                continue;
            }
            usb_devices.push_back(item);
        }
    } catch (const std::exception& e) {
        LOG_WARN("QueryUsbCameraList failed: {}", e.what());
    }

    std::sort(usb_devices.begin(), usb_devices.end(),
              [](const cosmo::camera::MsgUsbCameraDevice& a, const cosmo::camera::MsgUsbCameraDevice& b) {
                  return a.usbDeviceIndex < b.usbDeviceIndex;
              });

    LOG_INFO("USB camera list result count={}", usb_devices.size());
    return usb_devices;
}

// ============================================================
//  Channel instance accessors
// ============================================================

AlgChannelPtr CameraServiceImpl::GetChannelInst(const std::string& channelId) {
    return ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelInst(channelId);
}

constexpr int kTaskMonitorIntervalMs = 5000;
constexpr int kMemGcIntervalMs       = 60000 * 30;  // 30min GC interval

void CameraServiceImpl::InitCameraEntities() {
    LOG_INFO("{}", "CameraServiceImpl InitCameraEntities");
    LoadConfig();
    timer_ = std::make_unique<PeriodicTimer>("CameraMngTimer");
    timer_->Start();
    task_monitor_task_id_ = timer_->Schedule([this]() { CameraTaskMonitor(); }, kTaskMonitorIntervalMs);

    mem_gc_task_id_ = timer_->Schedule([this]() { MemGc(); }, kMemGcIntervalMs);
    LOG_INFO("{}", "CameraServiceImpl InitCameraEntities done");
}

}  // namespace cosmo::service

// JSON serialization for CameraEntity (only device config fields)
namespace cosmo::service {
void from_json(const nlohmann::json& j, CameraEntity& v) {
    if (j.contains("videoChannelId") && !j["videoChannelId"].is_null())
        j.at("videoChannelId").get_to(v.videoChannelId);
    if (j.contains("channelCode") && !j["channelCode"].is_null())
        j.at("channelCode").get_to(v.channelCode);
    if (j.contains("channelType") && !j["channelType"].is_null())
        j.at("channelType").get_to(v.channelType);
    if (j.contains("url") && !j["url"].is_null())
        j.at("url").get_to(v.url);
    if (j.contains("channelName") && !j["channelName"].is_null())
        j.at("channelName").get_to(v.channelName);
}

// Custom from_json for shared_ptr<CameraEntity> — CameraEntity is non-movable
// (has std::atomic, std::mutex, std::thread), so nlohmann's default shared_ptr
// deserialization (which requires T to be move-constructible) won't compile.
void from_json(const nlohmann::json& j, CameraEntityPtr& v) {
    v = std::make_shared<CameraEntity>();
    from_json(j, *v);
}

void to_json(nlohmann::json& j, const CameraEntity& v) {
    j["videoChannelId"] = v.videoChannelId;
    j["channelCode"]    = v.channelCode;
    j["channelType"]    = v.channelType;
    j["url"]            = v.url;
    j["channelName"]    = v.channelName;
}

}  // namespace cosmo::service

// JSON serialization for CameraTask (from CameraTaskMng.cc)
namespace cosmo {
void to_json(nlohmann::json& j, const CameraTask& v) {
    j["algorithmCode"] = v.algorithm_code_;
    j["scheduleId"]    = v.schedule_id_;
    j["algorithmName"] = v.algorithm_name_;
    j["scheduleName"]  = v.schedule_name_;
    j["switch"]        = v.is_enabled_;
}

void from_json(const nlohmann::json& j, CameraTask& v) {
    j.at("algorithmCode").get_to(v.algorithm_code_);
    j.at("scheduleId").get_to(v.schedule_id_);
    if (j.contains("algorithmName") && !j["algorithmName"].is_null())
        j.at("algorithmName").get_to(v.algorithm_name_);
    if (j.contains("scheduleName") && !j["scheduleName"].is_null())
        j.at("scheduleName").get_to(v.schedule_name_);
    if (j.contains("switch") && !j["switch"].is_null())
        j.at("switch").get_to(v.is_enabled_);
}

}  // namespace cosmo
