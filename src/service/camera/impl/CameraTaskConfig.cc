// CameraTaskConfig.cc — Task configuration, notification and query operations.
// All CameraTaskMng task-config methods are inlined here.

#include <algorithm>
#include <iterator>
#include <utility>

#include "flow/channel/AlgChannel.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IDeviceInfoService.h"
#include "service/task/IScheduleService.h"
#include "service/task/ITaskChannel.h"
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskQuery.h"
#include "util/Log.h"
#include "util/ScoreCalc.h"

namespace cosmo::service {

namespace {
    std::vector<ModelInfo> CollectModelsForAlgorithmNotify(const ActionAlgPtr& algData) {
        std::vector<ModelInfo> models;
        if (!algData) {
            return models;
        }

        auto& modelSvc = ServiceRegistry::Instance().Get<IModelService>();
        for (const auto& workFlow : algData->workFlow) {
            if (workFlow.atomicCode.empty()) {
                continue;
            }
            auto modelInfo = modelSvc.GetModelInfo(workFlow.atomicCode);
            if (modelInfo.id == workFlow.atomicCode) {
                models.push_back(modelInfo);
            }
        }
        return models;
    }
}  // namespace

// ============================================================
//  Task parameter / area / strategy / switch operations
// ============================================================

util::ErrorEnum CameraServiceImpl::CheckTaskStartResource() const {
#if defined(COSMO_NN_USE_SOPHON_BACKEND) || defined(COSMO_NN_USE_RKNN_BACKEND)
    if (!ServiceRegistry::Instance().Get<IConfigReadService>().GetResourceLimit()) {
        return util::ErrorEnum::Success;
    }

    size_t packet_total = 0, packet_proc = 0, packet_discard = 0, continues_discard_sec = 0;
    ServiceRegistry::Instance().Get<ITaskQuery>().PacketStatus(packet_total, packet_proc, packet_discard,
                                                               continues_discard_sec);
    double discard_percent = 0.0;
    if (packet_total > 0) {
        discard_percent = static_cast<double>(packet_discard) / static_cast<double>(packet_total);
    }
    auto gpu_info = ServiceRegistry::Instance().Get<IDeviceInfoService>().GetGpuUtilization();
    std::vector<GpuMemSnapshot> devs;
    devs.reserve(gpu_info.gpudevusage.size());
    std::transform(gpu_info.gpudevusage.begin(), gpu_info.gpudevusage.end(), std::back_inserter(devs),
                   [](const auto& d) { return GpuMemSnapshot{d.gpumemtotal, d.gpumemavailable}; });
    auto score = CalcCustomScore(gpu_info.gpuusage, gpu_info.gpumemtotal, gpu_info.gpumemavailable, devs,
                                 discard_percent, continues_discard_sec);
    if (score > 100.0) {
        LOG_WARN("Task start rejected by resource guard, score:{}", score);
        return util::ErrorEnum::ResourceLimit;
    }
#endif
    return util::ErrorEnum::Success;
}

std::string CameraServiceImpl::GetChannelName(const std::string& channelId) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    auto it = std::find_if(cameras_.begin(), cameras_.end(), [&](const CameraEntityPtr& camera) {
        return camera && camera->videoChannelId == channelId;
    });
    if (it != cameras_.end()) {
        return (*it)->channelName;
    }
    LOG_INFO("{} Not Exist", channelId);
    return "";
}

util::ErrorEnum CameraServiceImpl::SaveOrUpdateTask(const std::string& cameraId,
                                                    const std::string& algorithmId,
                                                    const MsgTaskConfig& params,
                                                    const std::string& scheduleId) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        LOG_INFO("{} Not Exist", cameraId);
        return util::ErrorEnum::CameraNotExist;
    }

    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        LOG_INFO("{} Is Being Deleted", cameraId);
        return util::ErrorEnum::CameraNotExist;
    }

    std::string scheduleName;
    if (!ServiceRegistry::Instance().Get<IScheduleService>().Exist(scheduleId, scheduleName)) {
        return util::ErrorEnum::TimeTemplateNotExist;
    }

    bool needs_enable = true;
    {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            if (!(*it)->task_ || !(*it)->task_->IsReady()) {
                LOG_WARN("[{}/{}] SaveOrUpdate skipped because task unit is not ready", cameraId,
                         algorithmId);
                return util::ErrorEnum::TaskCreateFailed;
            }
            needs_enable = !(*it)->is_enabled_.load(std::memory_order_acquire);
        } else if (camera->tasks_.size() >= camera->max_task_count_) {
            return util::ErrorEnum::TaskTooMuch;
        }
    }

    if (needs_enable) {
        auto resource_ret = CheckTaskStartResource();
        if (util::ErrorEnum::Success != resource_ret) {
            LOG_WARN("[{}/{}] SaveOrUpdate rejected before mutation: {}", cameraId, algorithmId,
                     static_cast<uint32_t>(resource_ret));
            return resource_ret;
        }
    }

    CameraTaskPtr task;
    bool start_task = false;
    {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            task = *it;
            MsgTaskConfig previous;
            previous.params = task->task_->GetParams();
            task->task_->GetArea(previous.areas, previous.shieldedAreas);

            auto ret = task->task_->SetParams(params);
            if (util::ErrorEnum::Success != ret) {
                auto rollback_ret = task->task_->SetParams(previous);
                if (util::ErrorEnum::Success != rollback_ret) {
                    LOG_ERRO("[{}/{}] SaveOrUpdate parameter rollback failed: {}", cameraId, algorithmId,
                             static_cast<uint32_t>(rollback_ret));
                }
                return ret;
            }
        } else {
            task                  = std::make_shared<CameraTask>();
            task->algorithm_code_ = algorithmId;
            auto ret              = MakeCameraTask(camera, task);
            if (util::ErrorEnum::Success != ret) {
                return ret;
            }
            ret = task->task_->SetParams(params);
            if (util::ErrorEnum::Success != ret) {
                return ret;
            }
            camera->tasks_.push_back(task);
        }

        const bool was_enabled = task->is_enabled_.load(std::memory_order_acquire);
        task->data_.taskConfig = params;
        task->schedule_id_     = scheduleId;
        task->schedule_name_   = scheduleName;
        task->is_enabled_.store(true, std::memory_order_release);
        SaveCameraTaskList(camera);
        start_task = !was_enabled;
    }

    if (start_task) {
        SwitchCameraTaskAsync(camera, task);
    }
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraServiceImpl::ModifyTaskParam(const std::string& cameraId,
                                                   const std::string& algorithmId, MsgTaskConfig& params) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);

        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            if (!(*it)->task_ || !(*it)->task_->IsReady()) {
                LOG_WARN("[{}/{}] SetParams skipped because task unit is not ready", cameraId, algorithmId);
                return util::ErrorEnum::TaskCreateFailed;
            }
            return (*it)->task_->SetParams(params);
        }
        CameraTaskPtr task    = std::make_shared<CameraTask>();
        task->algorithm_code_ = algorithmId;
        auto task_ret         = MakeCameraTask(camera, task);
        if (util::ErrorEnum::Success != task_ret) {
            return task_ret;
        }
        task->data_.taskConfig = params;
        auto ret               = task->task_->SetParams(params);
        camera->tasks_.push_back(task);
        SaveCameraTaskList(camera);
        return ret;
    });
}

util::ErrorEnum CameraServiceImpl::QueryTaskParam(const std::string& cameraId, const std::string& algorithmId,
                                                  std::vector<MsgDynamicKeyValue>& params) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);

        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            if (!(*it)->task_ || !(*it)->task_->IsReady()) {
                LOG_WARN("[{}/{}] GetParams skipped because task unit is not ready", cameraId, algorithmId);
                return util::ErrorEnum::TaskCreateFailed;
            }
            params = (*it)->task_->GetParams();
            return util::ErrorEnum::Success;
        }
        return util::ErrorEnum::TaskNotExist;
    });
}

util::ErrorEnum CameraServiceImpl::ModifyTaskArea(const std::string& cameraId, const std::string& algorithmId,
                                                  const std::vector<MsgTaskArea>& areas,
                                                  const std::vector<MsgTaskArea>& shieldedAreas) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);

        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            if (!(*it)->task_ || !(*it)->task_->IsReady()) {
                LOG_WARN("[{}/{}] SetArea skipped because task unit is not ready", cameraId, algorithmId);
                return util::ErrorEnum::TaskCreateFailed;
            }
            return (*it)->task_->SetArea(areas, shieldedAreas);
        }
        CameraTaskPtr task    = std::make_shared<CameraTask>();
        task->algorithm_code_ = algorithmId;
        auto task_ret         = MakeCameraTask(camera, task);
        if (util::ErrorEnum::Success != task_ret) {
            return task_ret;
        }
        auto ret = task->task_->SetArea(areas, shieldedAreas);
        camera->tasks_.push_back(task);
        SaveCameraTaskList(camera);
        return ret;
    });
}

util::ErrorEnum CameraServiceImpl::QueryTaskArea(const std::string& cameraId, const std::string& algorithmId,
                                                 std::vector<MsgTaskArea>& areas,
                                                 std::vector<MsgTaskArea>& shieldedAreas) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);

        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            if (!(*it)->task_ || !(*it)->task_->IsReady()) {
                LOG_WARN("[{}/{}] GetArea skipped because task unit is not ready", cameraId, algorithmId);
                return util::ErrorEnum::TaskCreateFailed;
            }
            (*it)->task_->GetArea(areas, shieldedAreas);
            return util::ErrorEnum::Success;
        }
        return util::ErrorEnum::TaskNotExist;
    });
}

util::ErrorEnum CameraServiceImpl::ModifyTaskStrategy(const std::string& cameraId,
                                                      const std::string& algorithmId,
                                                      const std::string& scheduleId) {
    std::string scheduleName;
    if (!ServiceRegistry::Instance().Get<IScheduleService>().Exist(scheduleId, scheduleName)) {
        return util::ErrorEnum::TimeTemplateNotExist;
    }

    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);

        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            if ((*it)->schedule_id_ != scheduleId) {
                (*it)->schedule_name_ = scheduleName;
                (*it)->schedule_id_   = scheduleId;
                SaveCameraTaskList(camera);
            }
            return util::ErrorEnum::Success;
        }
        CameraTaskPtr task    = std::make_shared<CameraTask>();
        task->algorithm_code_ = algorithmId;
        auto task_ret         = MakeCameraTask(camera, task);
        if (util::ErrorEnum::Success != task_ret) {
            return task_ret;
        }
        task->schedule_id_   = scheduleId;
        task->schedule_name_ = scheduleName;
        camera->tasks_.push_back(task);
        SaveCameraTaskList(camera);
        return util::ErrorEnum::Success;
    });
}

util::ErrorEnum CameraServiceImpl::QueryTaskStrategy(const std::string& cameraId,
                                                     const std::string& algorithmId,
                                                     std::string& scheduleId) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            scheduleId = (*it)->schedule_id_;
            return util::ErrorEnum::Success;
        }
        return util::ErrorEnum::TaskNotExist;
    });
}

bool CameraServiceImpl::ScheduleInUse(const std::string& scheduleId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return std::any_of(cameras_.begin(), cameras_.end(), [&scheduleId](const auto& camera) {
        std::shared_lock<std::shared_mutex> tlock(camera->task_mtx_);
        return std::any_of(camera->tasks_.begin(), camera->tasks_.end(),
                           [&scheduleId](const auto& task) { return task->schedule_id_ == scheduleId; });
    });
}

util::ErrorEnum CameraServiceImpl::SwitchTask(const std::string& cameraId, const std::string& algorithmId,
                                              bool enable) {
    // Authorization checks were removed; resource admission remains enforced for real start transitions.
    auto camera = GetCamera(cameraId);
    if (!camera) {
        LOG_INFO("{} Not Exist", cameraId);
        return util::ErrorEnum::CameraNotExist;
    }

    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        return util::ErrorEnum::CameraNotExist;
    }

    {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end() && (*it)->is_enabled_.load(std::memory_order_acquire) == enable) {
            return util::ErrorEnum::Success;
        }
    }

    if (enable) {
        auto resource_ret = CheckTaskStartResource();
        if (util::ErrorEnum::Success != resource_ret) {
            return resource_ret;
        }
    }

    CameraTaskPtr taskToSwitch = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);

        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            if ((*it)->is_enabled_ != enable) {
                (*it)->is_enabled_ = enable;
                SaveCameraTaskList(camera);
                taskToSwitch = *it;
            }
            if (!taskToSwitch) {
                return util::ErrorEnum::Success;
            }
        } else {
            CameraTaskPtr task    = std::make_shared<CameraTask>();
            task->algorithm_code_ = algorithmId;
            auto task_ret         = MakeCameraTask(camera, task);
            if (util::ErrorEnum::Success != task_ret) {
                return task_ret;
            }
            task->is_enabled_ = enable;
            camera->tasks_.push_back(task);
            SaveCameraTaskList(camera);
            taskToSwitch = task;
        }
    }
    // Execute expensive model destroy/rebuild/init asynchronously, freeing HTTP handler thread immediately
    SwitchCameraTaskAsync(camera, taskToSwitch);
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraServiceImpl::QuerySwitch(const std::string& cameraId, const std::string& algorithmId,
                                               bool& enable) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            enable = (*it)->is_enabled_;
            return util::ErrorEnum::Success;
        }
        return util::ErrorEnum::TaskNotExist;
    });
}

util::ErrorEnum CameraServiceImpl::AcquirePreviewChannel(const std::string& cameraId) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        LOG_INFO("{} Not Exist", cameraId);
        return util::ErrorEnum::CameraNotExist;
    }

    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        return util::ErrorEnum::CameraNotExist;
    }

    const size_t previous = camera->preview_lease_count_.fetch_add(1, std::memory_order_acq_rel);
    if (previous > 0) {
        return util::ErrorEnum::Success;
    }

    auto& lifecycle = ServiceRegistry::Instance().Get<ITaskLifecycle>();
    if (lifecycle.TaskIsStart(camera->channel_task_)) {
        return util::ErrorEnum::Success;
    }

    LOG_INFO("[{}] Auto-starting ChannelTask for live preview", cameraId);
    if (!lifecycle.TaskStart(cameraId, camera->channel_task_)) {
        camera->preview_lease_count_.fetch_sub(1, std::memory_order_acq_rel);
        LOG_WARN("[{}] Failed to auto-start ChannelTask for live preview", cameraId);
        return util::ErrorEnum::DemuxNoData;
    }
    return util::ErrorEnum::Success;
}

void CameraServiceImpl::ReleasePreviewChannel(const std::string& cameraId) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        return;
    }

    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        return;
    }

    size_t current = camera->preview_lease_count_.load(std::memory_order_acquire);
    while (current > 0 && !camera->preview_lease_count_.compare_exchange_weak(
                              current, current - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
    }
    if (current == 0) {
        LOG_WARN("[{}] Live preview lease release ignored because no lease is active", cameraId);
        return;
    }
    if (current > 1) {
        return;
    }

    // UpdateChannelState reads the count again, so a concurrent acquire cannot
    // have its channel stopped by this final release.
    UpdateChannelState(camera);
}

VideoFramePtr CameraServiceImpl::CaptureImage(const std::string& cameraId, int timeOutMs) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        LOG_INFO("{} Not Exist", cameraId);
        return nullptr;
    }

    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        return nullptr;
    }
    camera->WaitForSwitchThread();

    camera->is_capturing_image_.store(true);
    struct CaptureFlagGuard {
        std::atomic<bool>& flag;
        ~CaptureFlagGuard() {
            flag.store(false, std::memory_order_release);
        }
    } capture_guard{camera->is_capturing_image_};
    bool was_channel_running =
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(camera->channel_task_);

    int actualTimeOutMs = timeOutMs;
    if (!was_channel_running) {
        LOG_INFO("[{}] Temporarily auto-starting ChannelTask for CaptureImage", cameraId);
        if (!ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStart(cameraId, camera->channel_task_)) {
            LOG_WARN("[{}] Failed to auto-start ChannelTask for CaptureImage", cameraId);
            return nullptr;
        }

        // Cold starts require ffmpeg avformat_open_input and stream probing,
        // which can take several seconds for some files/streams.
        actualTimeOutMs = std::max(timeOutMs, 10000);
    }

    auto image = ServiceRegistry::Instance().Get<ITaskChannel>().CaptureImage(cameraId, actualTimeOutMs);

    // After successfully capturing a frame, cache video attributes (resolution/codec/fps)
    if (image) {
        MsgCameraAttr attr;
        if (ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelAttr(cameraId, attr)) {
            if (attr.width > 0 && attr.height > 0) {
                std::lock_guard<std::mutex> lock(camera->attr_mtx_);
                camera->cached_attr_ = attr;
                LOG_INFO("[{}] Cached video attr: {}x{} {} fps:{}", cameraId, attr.width, attr.height,
                         attr.codec, attr.fps);
            }
        }
    }

    if (!was_channel_running) {
        LOG_INFO("[{}] Temporarily auto-stopping ChannelTask after CaptureImage", cameraId);
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(camera->channel_task_);
        auto channel = ServiceRegistry::Instance().Get<ITaskChannel>().GetChannelInst(cameraId);
        if (channel)
            channel->Quit();
    }
    return image;
}

util::ErrorEnum CameraServiceImpl::DeleteTask(const std::string& cameraId, const std::string& algorithmId) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        camera->WaitForSwitchThread();
        CameraTaskUnitPtr task_unit;
        {
            std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);

            auto it =
                std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                             [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
            if (it == camera->tasks_.end()) {
                LOG_INFO("[{}/{}] Not Exist", cameraId, algorithmId);
                return util::ErrorEnum::TaskNotExist;
            }
            (*it)->is_enabled_.store(false, std::memory_order_release);
            task_unit = std::move((*it)->task_);
            camera->tasks_.erase(it);
            SaveCameraTaskList(camera);
        }
        // Camera task destruction stops/deletes the flow task.  No task_mtx_ is
        // held while worker threads are joined.
        task_unit.reset();
        UpdateChannelState(camera);
        return util::ErrorEnum::Success;
    });
}

std::vector<service::camera::CameraTaskDto> CameraServiceImpl::GetTasks(const std::string& cameraId) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        return {};
    }
    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        return {};
    }
    std::vector<service::camera::CameraTaskDto> taskInfos;
    std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
    for (auto& task : camera->tasks_) {
        service::camera::CameraTaskDto taskInfo;
        taskInfo.taskId        = task->task_id_;
        taskInfo.algorithmCode = task->algorithm_code_;
        taskInfo.algorithmName = task->algorithm_name_;
        taskInfo.scheduleId    = task->schedule_id_;
        taskInfo.scheduleName  = task->schedule_name_;
        taskInfo.enable        = task->is_enabled_;
        taskInfos.push_back(taskInfo);
    }
    return taskInfos;
}

// ============================================================
//  Algorithm notification (inlined from CameraTaskMngNotify)
// ============================================================

void CameraServiceImpl::NotifyAlgorithmsChanged(const std::vector<std::string>& algorithmIds,
                                                bool restartRunning) {
    if (algorithmIds.empty()) {
        return;
    }

    std::vector<CameraEntityPtr> cameraSnapshot;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        cameraSnapshot = cameras_;
    }

    if (!restartRunning) {
        for (const auto& camera : cameraSnapshot) {
            std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
            if (camera->deleting_) {
                continue;
            }
            camera->WaitForSwitchThread();
            for (const auto& algorithmId : algorithmIds) {
                // Update algorithm config under write lock, collect model-derived param refreshes
                struct RefreshRequest {
                    CameraTaskUnitPtr taskUnit;
                    ActionAlgPtr actionAlg;
                };
                std::vector<RefreshRequest> refreshRequests;
                {
                    std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
                    for (auto& task : camera->tasks_) {
                        if (!task || task->algorithm_code_ != algorithmId) {
                            continue;
                        }
                        task->action_alg_ = ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithm(
                            task->algorithm_code_);
                        if (task->task_ && task->task_->IsReady()) {
                            refreshRequests.push_back({task->task_, task->action_alg_});
                        } else {
                            LOG_WARN(
                                "[{}/{}] AlgorithmChanged -> skip model refresh because task unit is not "
                                "ready",
                                camera->videoChannelId, task->task_id_);
                        }
                        task->algorithm_name_ =
                            ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithmName(
                                task->algorithm_code_);
                    }
                    SaveCameraTaskList(camera);
                }

                // Refresh model-derived task params outside lock (avoid blocking other threads)
                for (const auto& req : refreshRequests) {
                    if (req.taskUnit) {
                        req.taskUnit->RefreshModels(CollectModelsForAlgorithmNotify(req.actionAlg));
                    }
                }
            }
        }
        return;
    }

    for (const auto& camera : cameraSnapshot) {
        std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
        if (camera->deleting_) {
            continue;
        }
        camera->WaitForSwitchThread();

        std::vector<std::vector<std::string>> tasks_to_restart;
        tasks_to_restart.reserve(algorithmIds.size());
        for (const auto& algorithmId : algorithmIds) {
            auto stopped_task_ids = StopAlgorithmForReload(camera, algorithmId);
            tasks_to_restart.push_back(std::move(stopped_task_ids));
        }
        for (const auto& algorithmId : algorithmIds) {
            RebuildAlgorithmForReload(camera, algorithmId);
        }
        for (const auto& task_ids : tasks_to_restart) {
            StartTasksAfterReload(camera, task_ids);
        }
    }
}

std::vector<std::string> CameraServiceImpl::StopAlgorithmForReload(const CameraEntityPtr& camera,
                                                                   const std::string& algorithmCode) {
    std::vector<std::string> taskIdsToRestart;
    {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        for (const auto& task : camera->tasks_) {
            if (!task || task->algorithm_code_ != algorithmCode) {
                continue;
            }
            taskIdsToRestart.push_back(task->task_id_);
        }
    }

    std::vector<std::string> stoppedTaskIds;
    for (const auto& taskId : taskIdsToRestart) {
        if (ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(taskId)) {
            LOG_INFO("[{}/{}] AlgorithmChanged -> stop running task for reload", camera->videoChannelId,
                     taskId);
            ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(taskId);
            stoppedTaskIds.push_back(taskId);
        }
    }
    return stoppedTaskIds;
}

void CameraServiceImpl::RebuildAlgorithmForReload(const CameraEntityPtr& camera,
                                                  const std::string& algorithmCode) {
    CameraTaskPtr taskToRebuild   = nullptr;
    CameraTaskUnitPtr oldTaskUnit = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
        for (auto& task : camera->tasks_) {
            if (!task || task->algorithm_code_ != algorithmCode) {
                continue;
            }
            task->action_alg_ =
                ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithm(task->algorithm_code_);
            task->algorithm_name_ =
                ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithmName(task->algorithm_code_);
            taskToRebuild = task;
            oldTaskUnit   = std::move(task->task_);
            break;
        }
        SaveCameraTaskList(camera);
    }

    if (!taskToRebuild) {
        return;
    }

    oldTaskUnit.reset();
    util::ErrorEnum rebuildRet;
    {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
        rebuildRet = MakeCameraTask(camera, taskToRebuild);
    }
    if (rebuildRet != util::ErrorEnum::Success) {
        LOG_WARN("[{}/{}] AlgorithmChanged -> rebuild task failed:{}", camera->videoChannelId, algorithmCode,
                 static_cast<uint32_t>(rebuildRet));
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
        taskToRebuild->task_.reset();
        taskToRebuild->status_ = CameraTaskStatus::kAbnormal;
    } else {
        LOG_INFO("[{}/{}] AlgorithmChanged -> rebuilt task for reload", camera->videoChannelId,
                 taskToRebuild->task_id_);
    }
}

void CameraServiceImpl::StartTasksAfterReload(const CameraEntityPtr& camera,
                                              const std::vector<std::string>& taskIds) {
    for (const auto& taskId : taskIds) {
        CameraTaskPtr task;
        {
            std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
            auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                                   [&](const CameraTaskPtr& candidate) {
                                       return candidate && candidate->task_id_ == taskId;
                                   });
            if (it != camera->tasks_.end()) {
                task = *it;
            }
        }
        if (!task) {
            continue;
        }

        bool restartOk = task->task_ && task->task_->IsReady() &&
                         task->task_->ApplyLatestTaskConfig(CameraTaskUnit::ParamApplyMode::kBeforeStart);
        if (restartOk) {
            restartOk = ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStart(camera->videoChannelId,
                                                                                     taskId);
        }
        {
            std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
            task->status_ = restartOk ? CameraTaskStatus::kInService : CameraTaskStatus::kAbnormal;
        }
    }
}

void CameraServiceImpl::NotifyAlgorithmsDeleted(const std::vector<std::string>& algorithmIds) {
    if (algorithmIds.empty()) {
        return;
    }
    std::vector<CameraEntityPtr> camera_snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        camera_snapshot = cameras_;
    }
    for (const auto& camera : camera_snapshot) {
        std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
        if (camera->deleting_) {
            continue;
        }
        camera->WaitForSwitchThread();
        for (const auto& algorithmId : algorithmIds) {
            // Phase 1: Mark status and collect taskIds to stop under write lock
            std::vector<std::string> taskIdsToStop;
            {
                std::lock_guard<std::shared_mutex> tlock(camera->task_mtx_);
                for (auto& task : camera->tasks_) {
                    if (!task || task->algorithm_code_ != algorithmId) {
                        continue;
                    }
                    taskIdsToStop.push_back(task->task_id_);
                    task->is_enabled_ = false;
                    task->status_     = CameraTaskStatus::kStop;
                    LOG_WARN("[{}/{}] AlgorithmDeleted -> task disabled", camera->videoChannelId,
                             task->task_id_);
                }
                SaveCameraTaskList(camera);
            }

            // Phase 2: Perform expensive TaskStop outside lock
            for (const auto& taskId : taskIdsToStop) {
                if (ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(taskId)) {
                    ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStop(taskId);
                }
            }
        }
    }
}

bool CameraServiceImpl::IsAlgorithmInUse(const std::string& algorithmId) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return std::any_of(cameras_.begin(), cameras_.end(), [&](const auto& camera) {
        std::shared_lock<std::shared_mutex> tlock(camera->task_mtx_);
        return std::any_of(camera->tasks_.begin(), camera->tasks_.end(),
                           [&algorithmId](const auto& task) { return task->algorithm_code_ == algorithmId; });
    });
}

// ============================================================
//  BindTaskLibPara
// ============================================================

util::ErrorEnum CameraServiceImpl::BindTaskLibPara(const std::string& cameraId,
                                                   const std::string& algorithmCode,
                                                   const std::vector<std::string>& bindLibs,
                                                   const std::string& paramKey) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        return util::ErrorEnum::NoSuchId;
    }
    std::lock_guard<std::mutex> command_lock(camera->command_mtx_);
    if (camera->deleting_) {
        return util::ErrorEnum::NoSuchId;
    }

    // Snapshot task_->task_ into a local shared_ptr under the shared lock so concurrent
    // RebuildAlgorithmForReload (which std::move/reset task_->task_ under the exclusive lock)
    // cannot null or destroy the unit mid-call. SetLibPara/SetParams then run lock-free on the
    // snapshot, keeping disk I/O (SaveLibPara/SaveParam) out of the lock scope. Mirrors the
    // NotifyAlgorithmsChanged snapshot idiom above.
    CameraTaskUnitPtr taskUnit;
    {
        std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& t) { return t->algorithm_code_ == algorithmCode; });
        if (it != camera->tasks_.end()) {
            taskUnit = (*it)->task_;
        }
    }
    if (!taskUnit) {
        return util::ErrorEnum::TaskNotExist;
    }

    auto mutable_bind_libs = bindLibs;
    auto errc              = taskUnit->SetLibPara(mutable_bind_libs);
    if (errc == util::ErrorEnum::Success) {
        MsgTaskConfig cfg;
        MsgDynamicKeyValue kv;
        kv.key   = paramKey;
        kv.value = util::JoinStrings(bindLibs);
        cfg.params.push_back(std::move(kv));
        if (taskUnit->IsReady()) {
            errc = taskUnit->SetParams(cfg);
        }
    }
    return errc;
}

}  // namespace cosmo::service
