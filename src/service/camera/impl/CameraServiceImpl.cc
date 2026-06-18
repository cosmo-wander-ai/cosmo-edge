// CameraServiceImpl.cc — Core lifecycle, config persistence, USB camera and utilities.
// Device CRUD operations are in CameraDeviceCrud.cc.
// Task configuration proxy is in CameraTaskConfig.cc.

#include "service/camera/impl/CameraServiceImpl.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <malloc.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <regex>

#include "flow/task/CameraTaskMng.h"
#include "service/camera/impl/CameraConfigPersistence.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/system/IConfigReadService.h"
#include "service/task/ITaskChannel.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {

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
    LOG_INFO("{}", "CameraServiceImpl Delete");
}

// ============================================================
//  Private helper methods — config persistence and lookup
// ============================================================

void CameraServiceImpl::LoadConfig() {
    cameras_       = detail::CameraConfigPersistence::LoadConfig(conf_file_path_, conf_file_name_);
    int max_number = -1;
    for (const auto& camera : cameras_) {
        LOG_INFO("LoadConfig channel Id {}", camera->videoChannelId);
        MakeTaskMng(camera);
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

void CameraServiceImpl::CameraTaskMonitor() {
    // Authorization removed: no longer check auth; always treat as authorized (matches old 23461e05)
    const bool is_service_authed = true;
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& camera : cameras_) {
        camera->taskMng->Monitor(is_service_authed);
    }
}

void CameraServiceImpl::MemGc() {
    LOG_INFO("{}", "malloctrim Start");
    malloc_trim(0);
    LOG_INFO("{}", "malloctrim End");
}

void CameraServiceImpl::MakeTaskMng(CameraEntityPtr camera) {
    camera->taskMng = std::make_shared<CameraTaskMng>(conf_file_path_, camera->videoChannelId, camera->url);
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

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
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

void to_json(nlohmann::json& j, const CameraEntity& v) {
    j["videoChannelId"] = v.videoChannelId;
    j["channelCode"]    = v.channelCode;
    j["channelType"]    = v.channelType;
    j["url"]            = v.url;
    j["channelName"]    = v.channelName;
}

}  // namespace cosmo::service
