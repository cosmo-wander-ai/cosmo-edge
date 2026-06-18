// CameraTaskConfig.cc — Task configuration proxy operations.
// Split from CameraServiceImpl.cc to reduce file size.

#include <algorithm>
#include <iterator>
#include <utility>

#include "flow/task/CameraTaskMng.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IDeviceInfoService.h"
#include "service/task/ITaskQuery.h"
#include "util/Log.h"
#include "util/ScoreCalc.h"

namespace cosmo::service {

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
    return WithCamera(cameraId,
                      [&](const CameraEntityPtr& c) { return c->taskMng->SetParams(algorithmId, params); });
}

util::ErrorEnum CameraServiceImpl::QueryTaskParam(const std::string& cameraId, const std::string& algorithmId,
                                                  std::vector<MsgDynamicKeyValue>& params) {
    return WithCamera(cameraId,
                      [&](const CameraEntityPtr& c) { return c->taskMng->GetParams(algorithmId, params); });
}

util::ErrorEnum CameraServiceImpl::ModifyTaskArea(const std::string& cameraId, const std::string& algorithmId,
                                                  const std::vector<MsgTaskArea>& areas,
                                                  const std::vector<MsgTaskArea>& shieldedAreas) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& c) {
        return c->taskMng->SetArea(algorithmId, areas, shieldedAreas);
    });
}

util::ErrorEnum CameraServiceImpl::QueryTaskArea(const std::string& cameraId, const std::string& algorithmId,
                                                 std::vector<MsgTaskArea>& areas,
                                                 std::vector<MsgTaskArea>& shieldedAreas) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& c) {
        return c->taskMng->GetArea(algorithmId, areas, shieldedAreas);
    });
}

util::ErrorEnum CameraServiceImpl::ModifyTaskStrategy(const std::string& cameraId,
                                                      const std::string& algorithmId,
                                                      const std::string& scheduleId) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& c) {
        return c->taskMng->SetStrategySchedule(algorithmId, scheduleId);
    });
}

util::ErrorEnum CameraServiceImpl::QueryTaskStrategy(const std::string& cameraId,
                                                     const std::string& algorithmId,
                                                     std::string& scheduleId) {
    return WithCamera(cameraId, [&](const CameraEntityPtr& c) {
        return c->taskMng->GetStrategySchedule(algorithmId, scheduleId);
    });
}

bool CameraServiceImpl::ScheduleInUse(const std::string& scheduleId) {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return std::any_of(cameras_.begin(), cameras_.end(), [&scheduleId](const auto& camera) {
        return camera->taskMng->ScheduleInUse(scheduleId);
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
    return WithCamera(cameraId,
                      [&](const CameraEntityPtr& c) { return c->taskMng->Switch(algorithmId, enable); });
}

util::ErrorEnum CameraServiceImpl::QuerySwitch(const std::string& cameraId, const std::string& algorithmId,
                                               bool& enable) {
    return WithCamera(cameraId,
                      [&](const CameraEntityPtr& c) { return c->taskMng->GetSwitch(algorithmId, enable); });
}

VideoFramePtr CameraServiceImpl::CaptureImage(const std::string& cameraId, int timeOutMs) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        LOG_INFO("{} Not Exist", cameraId);
        return nullptr;
    }
    return camera->taskMng->CaptureImage(timeOutMs);
}

util::ErrorEnum CameraServiceImpl::DeleteTask(const std::string& cameraId, const std::string& algorithmId) {
    return WithCamera(cameraId,
                      [&](const CameraEntityPtr& c) { return c->taskMng->DeleteTask(algorithmId); });
}

std::vector<service::camera::CameraTaskDto> CameraServiceImpl::GetTasks(const std::string& cameraId) {
    auto camera = GetCamera(cameraId);
    if (!camera) {
        return {};
    }
    return camera->taskMng->GetTasks();
}

void CameraServiceImpl::NotifyAlgorithmsChanged(const std::vector<std::string>& algorithmIds,
                                                bool restartRunning) {
    if (algorithmIds.empty()) {
        return;
    }

    std::vector<CameraTaskMngPtr> task_mngs;
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        for (const auto& camera : cameras_) {
            if (camera && camera->taskMng) {
                task_mngs.push_back(camera->taskMng);
            }
        }
    }

    if (!restartRunning) {
        for (const auto& task_mng : task_mngs) {
            for (const auto& algorithmId : algorithmIds) {
                task_mng->NotifyAlgorithmChanged(algorithmId, false);
            }
        }
        return;
    }

    std::vector<std::pair<CameraTaskMngPtr, std::vector<std::string>>> tasks_to_restart;
    for (const auto& task_mng : task_mngs) {
        for (const auto& algorithmId : algorithmIds) {
            auto stopped_task_ids = task_mng->StopAlgorithmForReload(algorithmId);
            if (!stopped_task_ids.empty()) {
                tasks_to_restart.push_back({task_mng, std::move(stopped_task_ids)});
            }
        }
    }

    for (const auto& task_mng : task_mngs) {
        for (const auto& algorithmId : algorithmIds) {
            task_mng->RebuildAlgorithmForReload(algorithmId);
        }
    }

    for (const auto& restart : tasks_to_restart) {
        if (restart.first) {
            restart.first->StartTasksAfterReload(restart.second);
        }
    }
}

void CameraServiceImpl::NotifyAlgorithmsDeleted(const std::vector<std::string>& algorithmIds) {
    if (algorithmIds.empty()) {
        return;
    }
    std::shared_lock<std::shared_mutex> lock(mtx_);
    for (const auto& camera : cameras_) {
        if (!camera || !camera->taskMng) {
            continue;
        }
        for (const auto& algorithmId : algorithmIds) {
            camera->taskMng->NotifyAlgorithmDeleted(algorithmId);
        }
    }
}

bool CameraServiceImpl::IsAlgorithmInUse(const std::string& algorithmId) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return std::any_of(cameras_.begin(), cameras_.end(), [&](const auto& camera) {
        if (!camera || !camera->taskMng) {
            return false;
        }
        auto tasks = camera->taskMng->GetTasks();
        return std::any_of(tasks.begin(), tasks.end(),
                           [&algorithmId](const auto& task) { return task.algorithmCode == algorithmId; });
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
    auto task = camera->taskMng->GetCameraTask(algorithmCode);
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
        errc = camera->taskMng->SetParams(algorithmCode, cfg);
    }
    return errc;
}

}  // namespace cosmo::service
