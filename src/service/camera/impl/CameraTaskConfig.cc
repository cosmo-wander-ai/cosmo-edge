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
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskChannel.h"
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
//  Per-camera task lookup helper
// ============================================================

CameraTaskPtr CameraServiceImpl::GetCameraTask(const CameraEntityPtr& camera,
                                               const std::string& algorithmCode) const {
    std::shared_lock<std::shared_mutex> lock(camera->task_mtx_);
    auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                           [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != camera->tasks_.end()) {
        return (*it);
    }
    return nullptr;
}

// ============================================================
//  Task parameter / area / strategy / switch operations
// ============================================================

std::string CameraServiceImpl::GetChannelName(const std::string& channelId) const {
    auto camera = GetCamera(channelId);
    if (!camera) {
        LOG_INFO("{} Not Exist", channelId);
        return "";
    }
    return camera->channelName;
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
    // Authorization removed: no longer reject task start due to auth/expiry/limit (matches old 23461e05)
#ifdef COSMO_NN_USE_SOPHON_BACKEND
    if (enable) {
        if (ServiceRegistry::Instance().Get<IConfigReadService>().GetResourceLimit()) {
            size_t packet_total = 0, packet_proc = 0, packet_discard = 0, continues_discard_sec = 0;
            ServiceRegistry::Instance().Get<ITaskQuery>().PacketStatus(packet_total, packet_proc,
                                                                       packet_discard, continues_discard_sec);
            double discard_percent = 0.0;
            if (packet_total > 0) {
                discard_percent = static_cast<double>(packet_discard) / static_cast<double>(packet_total);
            }
            auto gpu_info = ServiceRegistry::Instance().Get<IDeviceInfoService>().GetGpuUtilization();
            std::vector<GpuMemSnapshot> devs;
            devs.reserve(gpu_info.gpudevusage.size());
            std::transform(gpu_info.gpudevusage.begin(), gpu_info.gpudevusage.end(), std::back_inserter(devs),
                           [](const auto& d) {
                               return GpuMemSnapshot{d.gpumemtotal, d.gpumemavailable};
                           });
            auto score = CalcCustomScore(gpu_info.gpuusage, gpu_info.gpumemtotal, gpu_info.gpumemavailable,
                                         devs, discard_percent, continues_discard_sec);
            if (score > 100.0) {
                return util::ErrorEnum::ResourceLimit;
            }
        }
    }
#endif
    auto camera = GetCamera(cameraId);
    if (!camera) {
        LOG_INFO("{} Not Exist", cameraId);
        return util::ErrorEnum::CameraNotExist;
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

VideoFramePtr CameraServiceImpl::CaptureImage(const std::string& cameraId, int timeOutMs) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        LOG_INFO("{} Not Exist", cameraId);
        return nullptr;
    }

    camera->is_capturing_image_.store(true);
    bool was_channel_running =
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskIsStart(camera->channel_task_);

    int actualTimeOutMs = timeOutMs;
    if (!was_channel_running) {
        LOG_INFO("[{}] Temporarily auto-starting ChannelTask for CaptureImage", cameraId);
        ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStart(cameraId, camera->channel_task_);

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
    camera->is_capturing_image_.store(false);
    return image;
}

util::ErrorEnum CameraServiceImpl::DeleteTask(const std::string& cameraId, const std::string& algorithmId) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& camera) {
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);

        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& cfg) { return cfg->algorithm_code_ == algorithmId; });
        if (it != camera->tasks_.end()) {
            camera->tasks_.erase(it);
            SaveCameraTaskList(camera);
            return util::ErrorEnum::Success;
        }

        LOG_INFO("[{}/{}] Not Exist", cameraId, algorithmId);
        return util::ErrorEnum::TaskNotExist;
    });
}

std::vector<service::camera::CameraTaskDto> CameraServiceImpl::GetTasks(const std::string& cameraId) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
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

    std::vector<std::pair<CameraEntityPtr, std::vector<std::string>>> tasks_to_restart;
    for (const auto& camera : cameraSnapshot) {
        for (const auto& algorithmId : algorithmIds) {
            auto stopped_task_ids = StopAlgorithmForReload(camera, algorithmId);
            if (!stopped_task_ids.empty()) {
                tasks_to_restart.push_back({camera, std::move(stopped_task_ids)});
            }
        }
    }

    for (const auto& camera : cameraSnapshot) {
        for (const auto& algorithmId : algorithmIds) {
            RebuildAlgorithmForReload(camera, algorithmId);
        }
    }

    for (const auto& restart : tasks_to_restart) {
        if (restart.first) {
            StartTasksAfterReload(restart.first, restart.second);
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
        bool restartOk =
            ServiceRegistry::Instance().Get<ITaskLifecycle>().TaskStart(camera->videoChannelId, taskId);
        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& t) { return t && t->task_id_ == taskId; });
        if (it != camera->tasks_.end()) {
            (*it)->status_ = restartOk ? CameraTaskStatus::kInService : CameraTaskStatus::kAbnormal;
        }
    }
}

void CameraServiceImpl::NotifyAlgorithmsDeleted(const std::vector<std::string>& algorithmIds) {
    if (algorithmIds.empty()) {
        return;
    }
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& camera : cameras_) {
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
    auto task = GetCameraTask(camera, algorithmCode);
    if (!task || !task->task_) {
        return util::ErrorEnum::TaskNotExist;
    }

    auto mutable_bind_libs = bindLibs;
    auto errc              = task->task_->SetLibPara(mutable_bind_libs);
    if (errc == util::ErrorEnum::Success) {
        MsgTaskConfig cfg;
        MsgDynamicKeyValue kv;
        kv.key   = paramKey;
        kv.value = util::JoinStrings(bindLibs);
        cfg.params.push_back(std::move(kv));

        std::lock_guard<std::shared_mutex> lock(camera->task_mtx_);
        auto it = std::find_if(camera->tasks_.begin(), camera->tasks_.end(),
                               [&](const CameraTaskPtr& t) { return t->algorithm_code_ == algorithmCode; });
        if (it != camera->tasks_.end() && (*it)->task_ && (*it)->task_->IsReady()) {
            errc = (*it)->task_->SetParams(cfg);
        }
    }
    return errc;
}

}  // namespace cosmo::service
