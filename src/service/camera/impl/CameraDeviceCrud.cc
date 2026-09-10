// CameraDeviceCrud.cc — Camera device CRUD operations.
// Split from CameraServiceImpl.cc to reduce file size.
// CameraTaskMng calls are inlined — accesses CameraEntity directly.

#include <algorithm>
#include <filesystem>
#include <iomanip>  // std::setw, std::setfill
#include <sstream>  // std::ostringstream

#include "service/camera/impl/CameraServiceImpl.h"
#include "service/detail/ServiceRegistry.h"
#include "service/onvif/IOnvifService.h"
#include "service/task/ITaskChannel.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PaginationHelper.h"
#include "util/dto/ChannelStatusDto.h"

namespace cosmo::service {

namespace {
    bool IsDemuxError(int data_status) {
        return data_status == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxOpenFailed) ||
               data_status == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxReadFailed) ||
               data_status == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxInvalidUrl) ||
               data_status == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxOpenUnauthorized);
    }
}  // namespace

// Determine the effective task status based on demux state and channel type
static int DetermineTaskStatus(int current_status, int data_status, MsgCameraType channel_type) {
    if (current_status != static_cast<int>(CameraTaskStatus::kInService)) {
        return current_status;
    }
    bool is_demux_error = IsDemuxError(data_status);
    if (channel_type == MsgCameraType::MsgCameraTypeLive ||
        channel_type == MsgCameraType::MsgCameraTypeOnvif ||
        channel_type == MsgCameraType::MsgCameraTypeUsb ||
        channel_type == MsgCameraType::MsgCameraTypeGb28181) {
        if (is_demux_error) {
            return static_cast<int>(CameraTaskStatus::kAbnormal);
        }
        return current_status;
    }
    if (channel_type == MsgCameraType::MsgCameraTypeVod ||
        channel_type == MsgCameraType::MsgCameraTypeLocalVideo) {
        if (data_status == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxReadEnd)) {
            return static_cast<int>(CameraTaskStatus::kStop);
        }
        if (is_demux_error) {
            return static_cast<int>(CameraTaskStatus::kAbnormal);
        }
    }
    return current_status;
}

// ============================================================
//  Core device management — Add / Update / Delete / Query
// ============================================================

util::ErrorEnum CameraServiceImpl::Add(MsgCameraInfo& config, std::string& id) {
    std::lock_guard<std::mutex> crud_lock(camera_crud_mtx_);
    if (stopping_.load(std::memory_order_acquire)) {
        LOG_WARN("{}", "Reject camera creation during service shutdown");
        return util::ErrorEnum::SysErr;
    }
    // Generate and reserve the channel ID logically while CRUD operations are
    // serialized. Slow file/task/probe work is performed without mtx_ held.
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        if (cameras_.size() >= max_camera_count_) {
            return util::ErrorEnum::CameraCountLimit;
        }
        if (config.videoChannelId.empty()) {
            channel_code_num_++;
            std::ostringstream oss;
            if (MsgCameraType::MsgCameraTypeLocalVideo == config.channelType) {
                oss << "LX";
            } else if (MsgCameraType::MsgCameraTypeUsb == config.channelType) {
                oss << "US";
            } else if (MsgCameraType::MsgCameraTypeGb28181 == config.channelType) {
                oss << "GB";
            } else {
                oss << "RT";
            }
            oss << std::setw(10) << std::setfill('0') << channel_code_num_;
            config.videoChannelId = oss.str();
        }
        id = config.videoChannelId;
        if (std::any_of(cameras_.begin(), cameras_.end(), [&](const CameraEntityPtr& camera) {
                return camera && camera->videoChannelId == id;
            })) {
            return util::ErrorEnum::IDExist;
        }
    }

    CameraEntityPtr camera = std::make_shared<service::CameraEntity>();
    camera->videoChannelId = config.videoChannelId;
    camera->channelCode    = config.channelCode;
    camera->channelName    = config.channelName;
    if (MsgCameraType::MsgCameraTypeLocalVideo == config.channelType) {
        if (!util::FileExist(config.url)) {
            return util::ErrorEnum::FileNotExist;
        }
        camera->url = GetVideoFileName(id, config.url);
        LOG_INFO("camera->url:{}", camera->url);
        if (!util::FileMoveWithRename(config.url, camera->url)) {
            LOG_WARN("Failed to move local video {} to {}", config.url, camera->url);
            return util::ErrorEnum::FileMoveFailed;
        }
    } else {
        std::string logical_url;
        std::string media_url;
        if (!ResolveSourceUrl(config.channelType, config.url, logical_url, media_url)) {
            LOG_WARN("Invalid GB28181 source: {}", config.url);
            return util::ErrorEnum::InvalidParam;
        }
        camera->url          = logical_url;
        camera->channel_url_ = media_url;
        config.url           = camera->url;
    }

    camera->channelType = static_cast<int>(config.channelType);
    InitCameraChannel(camera);
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        cameras_.push_back(camera);
    }

    // Immediately trigger a health probe to quickly update channel status to online.
    // This may block for up to the TCP probe timeout, so it must not hold mtx_.
    ProbeCameraOnlineStatusNow(camera);
    SaveConfig();
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraServiceImpl::Update(MsgCameraInfo& config) {
    std::lock_guard<std::mutex> crud_lock(camera_crud_mtx_);
    auto camera = GetCamera(config.videoChannelId);
    if (!camera) {
        LOG_INFO("{} Not Exist", config.videoChannelId);
        return util::ErrorEnum::CameraNotExist;
    }
    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        return util::ErrorEnum::CameraNotExist;
    }
    camera->WaitForSwitchThread();
    const auto source_type  = static_cast<MsgCameraType>(camera->channelType);
    std::string logical_url = camera->url;
    std::string media_url   = camera->channel_url_;
    if (source_type != MsgCameraType::MsgCameraTypeLocalVideo) {
        if (!ResolveSourceUrl(source_type, config.url, logical_url, media_url)) {
            LOG_WARN("Invalid GB28181 source: {}", config.url);
            return util::ErrorEnum::InvalidParam;
        }
        config.url = logical_url;
    }

    bool channel_url_changed = false;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);

        camera->videoChannelId = config.videoChannelId;
        camera->channelCode    = config.channelCode;
        camera->channelName    = config.channelName;
        if (source_type != MsgCameraType::MsgCameraTypeLocalVideo) {
            camera->url = logical_url;
        }
        // Update channel URL directly (inlined from CameraTaskMng::SetChannelUrl)
        channel_url_changed  = camera->channel_url_ != media_url;
        camera->channel_url_ = media_url;
        LOG_INFO("{}/{} Update", config.videoChannelId, config.channelName);
    }
    if (channel_url_changed) {
        ServiceRegistry::Instance().Get<ITaskChannel>().TaskChannelSetUrl(camera->videoChannelId, media_url);
    }
    // Editing channel may have changed URL; trigger immediate probe to update status.
    ProbeCameraOnlineStatusNow(camera);
    SaveConfig();
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraServiceImpl::Delete(const std::string& videoChannelId) {
    std::lock_guard<std::mutex> crud_lock(camera_crud_mtx_);
    LOG_INFO("Delete started for camera: {}", videoChannelId);

    // Fast lookup and remove (minimize lock scope)
    CameraEntityPtr target;
    bool need_remove_file = false;
    std::string file_to_remove;

    target = GetCamera(videoChannelId);
    if (!target) {
        LOG_INFO("Camera {} not found for deletion", videoChannelId);
        return util::ErrorEnum::CameraNotExist;
    }
    {
        // command_mtx_ is always acquired before mtx_. This matches Update()
        // and avoids a delete-vs-command ABBA deadlock.
        std::lock_guard<std::mutex> command_lock(target->command_mtx_);
        target->deleting_ = true;
        need_remove_file =
            (MsgCameraType::MsgCameraTypeLocalVideo == static_cast<MsgCameraType>(target->channelType));
        if (need_remove_file) {
            file_to_remove = target->url;  // Copy URL to avoid accessing object later
        }

        std::lock_guard<std::shared_mutex> lock(mtx_);
        auto it = std::find(cameras_.begin(), cameras_.end(), target);
        if (it == cameras_.end()) {
            return util::ErrorEnum::CameraNotExist;
        }
        LOG_INFO("Deleting camera: {}/{}", target->videoChannelId, target->channelName);
        cameras_.erase(it);
    }

    // Destroy channel tasks (stop algo tasks + channel task)
    DestroyCameraChannel(target);

    // Perform potentially slow file operations (lockless)
    if (need_remove_file) {
        LOG_INFO("Removing associated file: {}", file_to_remove);
        try {
            util::RemoveFile(file_to_remove);
            LOG_INFO("{}", "File removed successfully");
        } catch (const std::exception& e) {
            LOG_ERRO("File removal failed: {}", e.what());
        }
    }

    // Save config (thread-safe)
    SaveConfig();
    if (static_cast<MsgCameraType>(target->channelType) == MsgCameraType::MsgCameraTypeOnvif &&
        ServiceRegistry::Instance().Has<IOnvifService>()) {
        try {
            ServiceRegistry::Instance().Get<IOnvifService>().Remove(target->url);
        } catch (const std::exception&) {
            LOG_WARN("{}", "ONVIF orphan configuration cleanup failed");
        }
    }
    LOG_INFO("Delete completed for camera: {}", videoChannelId);
    return util::ErrorEnum::Success;
}

std::vector<MsgCameraInfo> CameraServiceImpl::Query(const std::string& channelName, int channelStatus,
                                                    int pageNum, int pageSize, size_t& total) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return util::PaginationHelper::Paginate(
        cameras_.rbegin(), cameras_.rend(), pageNum, pageSize, total,
        [&](const auto& info_cfg) {
            if (!channelName.empty() && std::string::npos == info_cfg->channelName.find(channelName)) {
                return false;
            }
            if (static_cast<ChannelStatus>(channelStatus) != ChannelStatus::ChannelStatusAll) {
                MsgCameraInfo info;
                info.channelType = static_cast<MsgCameraType>(info_cfg->channelType);
                ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelAttr(info_cfg->videoChannelId,
                                                                               info);
                if (info.dataStatus == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxInit) ||
                    info.dataStatus == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxClosed) ||
                    info.channelStatus == ChannelStatus::ChannelStatusOffline) {
                    auto probed_status = info_cfg->probed_status_.load();
                    if (probed_status == ChannelStatus::ChannelStatusOnline ||
                        probed_status == ChannelStatus::ChannelStatusOffline) {
                        info.channelStatus = probed_status;
                    }
                }
                if (static_cast<ChannelStatus>(channelStatus) != info.channelStatus) {
                    return false;
                }
            }
            return true;
        },
        [&](const auto& info_cfg) {
            MsgCameraInfo info;
            info.videoChannelId = info_cfg->videoChannelId;
            info.channelCode    = info_cfg->channelCode;
            info.channelName    = info_cfg->channelName;
            info.channelType    = static_cast<MsgCameraType>(info_cfg->channelType);
            info.url            = info_cfg->url;

            // Get resolution and online status info
            ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelAttr(info.videoChannelId, info);

            // If underlying stream has not started, use lightweight probe for status
            if (info.dataStatus == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxInit) ||
                info.dataStatus == static_cast<int>(camera::AlgDemuxStatus::AlgDemuxClosed) ||
                info.channelStatus == ChannelStatus::ChannelStatusOffline) {
                auto probed_status = info_cfg->probed_status_.load();
                if (probed_status == ChannelStatus::ChannelStatusOnline ||
                    probed_status == ChannelStatus::ChannelStatusOffline) {
                    info.channelStatus = probed_status;
                }
            }

            // Channel not started; fill resolution/encoding/fps from cached video attrs
            if (info.width == 0 || info.height == 0) {
                MsgCameraAttr cached_attr;
                {
                    std::lock_guard<std::mutex> alock(info_cfg->attr_mtx_);
                    cached_attr = info_cfg->cached_attr_;
                }
                if (cached_attr.width > 0 && cached_attr.height > 0) {
                    info.width  = cached_attr.width;
                    info.height = cached_attr.height;
                    info.codec  = cached_attr.codec;
                    info.fps    = cached_attr.fps;
                }
            }

            // Build task list directly from CameraEntity tasks
            size_t total_task = 0;
            {
                std::shared_lock<std::shared_mutex> tlock(info_cfg->task_mtx_);
                total_task    = info_cfg->tasks_.size();
                info.taskList = util::PaginationHelper::PaginateKnownTotal(
                    info_cfg->tasks_.begin(), info_cfg->tasks_.end(), 1, 100, total_task,
                    [](const auto& taskPtr) {
                        MsgCameraTask taskInfo;
                        taskInfo.algorithmId   = taskPtr->algorithm_code_;
                        taskInfo.algorithmName = taskPtr->algorithm_name_;
                        taskInfo.scheduleName  = taskPtr->schedule_name_;
                        taskInfo.scheduleId    = taskPtr->schedule_id_;
                        taskInfo.enable        = taskPtr->is_enabled_;
                        taskInfo.status        = static_cast<int>(taskPtr->status_.load());
                        return taskInfo;
                    });
            }
            if (info.dataStatus != static_cast<int>(camera::AlgDemuxStatus::AlgDemuxReading)) {
                for (auto& task : info.taskList) {
                    task.status = DetermineTaskStatus(task.status, info.dataStatus, info.channelType);
                }
            }
            return info;
        });
}

}  // namespace cosmo::service
