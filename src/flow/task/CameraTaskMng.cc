// Camera task manager — core operations: create, switch, query, delete tasks.

#include "flow/task/CameraTaskMng.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "flow/channel/AlgChannel.h"
#include "flow/common/AlgDataRecord.h"
#include "flow/common/FlowTaskUtil.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/detail/ServiceRegistry.h"
#include "service/model/IModelQuery.h"
#include "service/model/IModelService.h"
#include "service/task/IScheduleService.h"
#include "service/task/ITaskChannel.h"
#include "service/task/ITaskLifecycle.h"
#include "util/Exec.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/Log.h"
#include "util/PaginationHelper.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"
#include "util/dto/ChannelStatusDto.h"

namespace cosmo {
CameraTaskMng::CameraTaskMng(const std::string& cameraCfgPath, const std::string& cameraId,
                             const std::string& channelUrl)
    : conf_file_path_((std::filesystem::path(cameraCfgPath) / cameraId).string()),
      channel_id_(cameraId),
      channel_task_(cameraId + "-ChannelTask"),
      channel_url_(channelUrl) {
    LoadTaskList();
    ActionAlgPtr action_alg   = std::make_shared<ActionAlg>();
    action_alg->algorithmCode = "Channel";
    // Create a channel task for frame capture (RTSP streams or local video files).
    // Actual Start is deferred until an analysis task is enabled, saving NPU and bandwidth.
    service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskCreate(
        channel_id_, channel_id_, channel_task_, action_alg);
    service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().TaskChannelSetUrl(channel_id_,
                                                                                               channel_url_);
    LOG_INFO("[{}] CameraTaskMng Init", channel_id_);
}

CameraTaskMng::~CameraTaskMng() {
    // 1. Wait for async switch thread to complete, preventing use-after-free
    WaitForSwitchThread();

    // 2. Explicitly stop/delete all algorithm tasks (do not rely on implicit vector destruction).
    //    Must be done before channel task deletion to ensure AlgChannel refcount decrements correctly.
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        for (auto& task : tasks_) {
            if (task && task->task_) {
                // CameraTaskUnit destructor calls TaskStop + TaskDelete.
                // Explicit reset triggers destruction before channel task deletion.
                LOG_INFO("[{}] ~CameraTaskMng: Destroying algo task {}", channel_id_, task->algorithm_code_);
                task->task_.reset();
            }
        }
        tasks_.clear();
    }

    // 3. Finally stop/delete the channel task (all algorithm tasks are cleaned up, AlgChannel can be safely
    // destroyed)
    service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(channel_task_);
    service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskDelete(channel_task_);
    LOG_INFO("[{}] CameraTaskMng Delete", channel_id_);
}

VideoFramePtr CameraTaskMng::CaptureImage(int timeOutMs) {
    is_capturing_image_.store(true);
    bool was_channel_running =
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskIsStart(channel_task_);

    int actualTimeOutMs = timeOutMs;
    if (!was_channel_running) {
        LOG_INFO("[{}] Temporarily auto-starting ChannelTask for CaptureImage", channel_id_);
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStart(channel_id_,
                                                                                             channel_task_);

        // Cold starts require ffmpeg avformat_open_input and stream probing,
        // which can take several seconds for some files/streams.
        actualTimeOutMs = std::max(timeOutMs, 10000);
    }

    auto image = service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().CaptureImage(
        channel_id_, actualTimeOutMs);

    // After successfully capturing a frame, cache video attributes (resolution/codec/fps)
    if (image) {
        MsgCameraAttr attr;
        if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelAttr(
                channel_id_, attr)) {
            if (attr.width > 0 && attr.height > 0) {
                std::lock_guard<std::mutex> lock(attr_mtx_);
                cached_attr_ = attr;
                LOG_INFO("[{}] Cached video attr: {}x{} {} fps:{}", channel_id_, attr.width, attr.height,
                         attr.codec, attr.fps);
            }
        }
    }

    if (!was_channel_running) {
        LOG_INFO("[{}] Temporarily auto-stopping ChannelTask after CaptureImage", channel_id_);
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(channel_task_);
        auto channel =
            service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().GetChannelInst(
                channel_id_);
        if (channel)
            channel->Quit();
    }
    is_capturing_image_.store(false);
    return image;
}

ChannelStatus CameraTaskMng::GetProbedStatus() const {
    return probed_status_.load();
}

MsgCameraAttr CameraTaskMng::GetCachedAttr() const {
    std::lock_guard<std::mutex> lock(attr_mtx_);
    return cached_attr_;
}

void CameraTaskMng::SaveTaskList() {
    auto path = (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_task_list_).string();
    (void)util::SaveStructToJsonFile(path, tasks_);
}

void CameraTaskMng::SetChannelUrl(const std::string& channelUrl) {
    if (channel_url_ != channelUrl) {
        // Required for live streams
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskChannel>().TaskChannelSetUrl(
            channel_id_, channelUrl);
    }
    channel_url_ = channelUrl;
}
void CameraTaskMng::LoadTaskList() {
    auto cfgPath =
        (std::filesystem::path(cosmo::path::GetCfgPath(conf_file_path_)) / conf_task_list_).string();
    (void)util::LoadStructFromJsonFile(cfgPath, tasks_);
    for (auto it = tasks_.begin(); it != tasks_.end();) {
        auto alg_temp = MakeTask(*it);
        if (util::ErrorEnum::Success != alg_temp) {
            auto algCode = (*it)->algorithm_code_;
            it           = tasks_.erase(it);
            LOG_WARN("{} Make Task failed", algCode);
        } else {
            it++;
        }
    }
}

util::ErrorEnum CameraTaskMng::MakeTask(CameraTaskPtr task) {
    if (tasks_.size() >= camera_max_task_count_) {
        return util::ErrorEnum::TaskTooMuch;
    }
    auto algData = service::ServiceRegistry::Instance().Get<service::IAlgorithmQuery>().GetAlgorithm(
        task->algorithm_code_);
    if (!algData) {
        return util::ErrorEnum::ActionAlgNotExist;
    }

    task->action_alg_ = algData;

    std::vector<ModelInfo> models;
    for (const auto& workFlow : algData->workFlow) {
        if (!workFlow.atomicCode.empty()) {
            LOG_INFO("[{}/{}] [{}/{}]", workFlow.actionId, workFlow.actionName, workFlow.atomicCode,
                     workFlow.atomAlgName);
            auto modelInfo = service::ServiceRegistry::Instance().Get<service::IModelService>().GetModelInfo(
                workFlow.atomicCode);
            if (modelInfo.id == workFlow.atomicCode) {
                models.push_back(modelInfo);
            }
        }
    }

    if (task->schedule_id_.empty()) {
        task->schedule_id_ =
            service::ServiceRegistry::Instance().Get<service::IScheduleService>().GetDefaultId();
    }

    // set taskId
    task->task_id_        = ChannelAlgIdToTaskId(channel_id_, task->algorithm_code_);
    task->algorithm_name_ = algData->algorithmName;
    auto taskUnit =
        std::make_shared<CameraTaskUnit>(conf_file_path_, channel_id_, task->algorithm_code_, models);
    if (!taskUnit->IsReady()) {
        auto status = taskUnit->GetStatus();
        LOG_WARN("[{}/{}] Make Task failed, unit status:{}", channel_id_, task->algorithm_code_,
                 static_cast<uint32_t>(status));
        return status;
    }
    task->task_ = std::move(taskUnit);
    LOG_INFO("[{}/{}] Make Task:{}", channel_id_, task->algorithm_code_, task->task_id_);
    return util::ErrorEnum::Success;
}

util::ErrorEnum CameraTaskMng::DeleteTask(const std::string& algorithmCode) {
    std::lock_guard<std::shared_mutex> lock(mtx_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        tasks_.erase(it);
        SaveTaskList();
        return util::ErrorEnum::Success;
    }

    LOG_INFO("[{}/{}] Not Exist", channel_id_, algorithmCode);
    return util::ErrorEnum::TaskNotExist;
}

util::ErrorEnum CameraTaskMng::SetArea(const std::string& algorithmCode,
                                       const std::vector<MsgTaskArea>& areas,
                                       const std::vector<MsgTaskArea>& shieldedAreas) {
    std::lock_guard<std::shared_mutex> lock(mtx_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        if (!(*it)->task_ || !(*it)->task_->IsReady()) {
            LOG_WARN("[{}/{}] SetArea skipped because task unit is not ready", channel_id_, algorithmCode);
            return util::ErrorEnum::TaskCreateFailed;
        }
        return (*it)->task_->SetArea(areas, shieldedAreas);
    }
    CameraTaskPtr task    = std::make_shared<CameraTask>();
    task->algorithm_code_ = algorithmCode;

    auto task_ret = MakeTask(task);
    if (util::ErrorEnum::Success != task_ret) {
        return task_ret;
    }
    auto ret = task->task_->SetArea(areas, shieldedAreas);
    tasks_.push_back(task);
    SaveTaskList();
    return ret;
}

util::ErrorEnum CameraTaskMng::GetArea(const std::string& algorithmCode, std::vector<MsgTaskArea>& areas,
                                       std::vector<MsgTaskArea>& shieldedAreas) {
    std::shared_lock<std::shared_mutex> lock(mtx_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        if (!(*it)->task_ || !(*it)->task_->IsReady()) {
            LOG_WARN("[{}/{}] GetArea skipped because task unit is not ready", channel_id_, algorithmCode);
            return util::ErrorEnum::TaskCreateFailed;
        }
        (*it)->task_->GetArea(areas, shieldedAreas);
        return util::ErrorEnum::Success;
    }
    return util::ErrorEnum::TaskNotExist;
}

util::ErrorEnum CameraTaskMng::SetParams(const std::string& algorithmCode, MsgTaskConfig& params) {
    std::lock_guard<std::shared_mutex> lock(mtx_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        if (!(*it)->task_ || !(*it)->task_->IsReady()) {
            LOG_WARN("[{}/{}] SetParams skipped because task unit is not ready", channel_id_, algorithmCode);
            return util::ErrorEnum::TaskCreateFailed;
        }
        return (*it)->task_->SetParams(params);
    }
    CameraTaskPtr task    = std::make_shared<CameraTask>();
    task->algorithm_code_ = algorithmCode;

    auto task_ret = MakeTask(task);
    if (util::ErrorEnum::Success != task_ret) {
        return task_ret;
    }
    task->data_.taskConfig = params;
    auto ret               = task->task_->SetParams(params);
    tasks_.push_back(task);
    SaveTaskList();
    return ret;
}

util::ErrorEnum CameraTaskMng::GetParams(const std::string& algorithmCode,
                                         std::vector<MsgDynamicKeyValue>& params) {
    std::shared_lock<std::shared_mutex> lock(mtx_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        if (!(*it)->task_ || !(*it)->task_->IsReady()) {
            LOG_WARN("[{}/{}] GetParams skipped because task unit is not ready", channel_id_, algorithmCode);
            return util::ErrorEnum::TaskCreateFailed;
        }
        params = (*it)->task_->GetParams();
        return util::ErrorEnum::Success;
    }

    return util::ErrorEnum::TaskNotExist;
}

void CameraTaskMng::MakeTaskOverviewParam(CameraTaskPtr task) {
    if (!task || !task->task_ || !task->task_->IsReady()) {
        LOG_WARN("[{}] MakeTaskOverviewParam skipped because task unit is not ready",
                 task ? task->task_id_ : "");
        return;
    }
    RecordAlgDataClearTaskData(task->task_id_);
    task->task_->GetArea(task->data_.taskConfig.areas, task->data_.taskConfig.shieldedAreas);
    task->data_.taskConfig.params = task->task_->GetParams();
    task->data_.streamUrl         = channel_url_;
    RecordAlgTaskInfo(task->task_id_, task->data_);
    RecordAlgTaskAction(task->task_id_, task->action_alg_);
}

void CameraTaskMng::SwitchTask(CameraTaskPtr task) {
    if (!task) {
        LOG_WARN("[{}] SwitchTask skipped because task is null", channel_id_);
        return;
    }
    if (!task->task_ || !task->task_->IsReady()) {
        LOG_WARN("[{}/{}] SwitchTask skipped because task unit is not ready", channel_id_, task->task_id_);
        task->status_ = CameraTaskStatus::kAbnormal;
        return;
    }
    if (task->is_enabled_) {
        MakeTaskOverviewParam(task);

        if (service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStart(
                channel_id_, task->task_id_)) {
            task->status_ = CameraTaskStatus::kInService;
        } else {
            task->status_ = CameraTaskStatus::kAbnormal;
        }
    } else {
        service::ServiceRegistry::Instance().Get<cosmo::service::ITaskLifecycle>().TaskStop(task->task_id_);
        task->status_ = CameraTaskStatus::kStop;
    }
    UpdateChannelTaskState();
}

util::ErrorEnum CameraTaskMng::Switch(const std::string& algorithmCode, bool enable) {
    CameraTaskPtr taskToSwitch = nullptr;
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);

        auto it = std::find_if(tasks_.begin(), tasks_.end(), [&](const CameraTaskPtr cfg) {
            return cfg->algorithm_code_ == algorithmCode;
        });
        if (it != tasks_.end()) {
            if ((*it)->is_enabled_ != enable) {
                (*it)->is_enabled_ = enable;
                SaveTaskList();
                taskToSwitch = *it;
            }
            if (!taskToSwitch) {
                return util::ErrorEnum::Success;
            }
        } else {
            CameraTaskPtr task    = std::make_shared<CameraTask>();
            task->algorithm_code_ = algorithmCode;

            auto task_ret = MakeTask(task);
            if (util::ErrorEnum::Success != task_ret) {
                return task_ret;
            }
            task->is_enabled_ = enable;
            tasks_.push_back(task);
            SaveTaskList();
            taskToSwitch = task;
        }
    }
    // Execute expensive model destroy/rebuild/init asynchronously, freeing HTTP handler thread immediately
    SwitchTaskAsync(taskToSwitch);
    return util::ErrorEnum::Success;
}

void CameraTaskMng::SwitchTaskAsync(CameraTaskPtr task) {
    // Wait for the previous switch thread to finish (only one switch per channel at a time)
    WaitForSwitchThread();

    std::string channelId = channel_id_;
    switch_thread_        = std::thread([this, task, channelId]() {
        LOG_INFO("[{}/{}] SwitchTaskAsync START in background thread", channelId, task->task_id_);
        SwitchTask(task);
        LOG_INFO("[{}/{}] SwitchTaskAsync DONE", channelId, task->task_id_);
    });
}

void CameraTaskMng::WaitForSwitchThread() {
    if (switch_thread_.joinable()) {
        switch_thread_.join();
    }
}

util::ErrorEnum CameraTaskMng::GetSwitch(const std::string& algorithmCode, bool& enable) {
    std::shared_lock<std::shared_mutex> lock(mtx_);

    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const CameraTaskPtr cfg) { return cfg->algorithm_code_ == algorithmCode; });
    if (it != tasks_.end()) {
        enable = (*it)->is_enabled_;
        return util::ErrorEnum::Success;
    }
    return util::ErrorEnum::TaskNotExist;
}

// GetTasks, NotifyAlgorithm*, GetCameraTask — moved to CameraTaskMngNotify.cc

bool CameraTaskMng::ScheduleInUse(const std::string& scheduleId) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return std::any_of(tasks_.begin(), tasks_.end(),
                       [&scheduleId](const auto& task) { return task->schedule_id_ == scheduleId; });
}

std::vector<MsgCameraTask> CameraTaskMng::Query(int pageNum, int pageSize, size_t& total) const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    total = tasks_.size();
    return util::PaginationHelper::PaginateKnownTotal(tasks_.begin(), tasks_.end(), pageNum, pageSize, total,
                                                      [](const auto& info) {
                                                          MsgCameraTask taskInfo;
                                                          taskInfo.algorithmId   = info->algorithm_code_;
                                                          taskInfo.algorithmName = info->algorithm_name_;
                                                          taskInfo.scheduleName  = info->schedule_name_;
                                                          taskInfo.scheduleId    = info->schedule_id_;
                                                          taskInfo.enable        = info->is_enabled_;
                                                          taskInfo.status = static_cast<int>(info->status_);
                                                          return taskInfo;
                                                      });
}

// Monitor, UpdateChannelTaskState, ProbeOnlineStatus — moved to CameraTaskMngMonitor.cc

}  // namespace cosmo

// JSON serialization for CameraTask
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
