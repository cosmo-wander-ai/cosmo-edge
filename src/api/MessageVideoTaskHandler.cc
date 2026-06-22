// MessageVideoTaskHandler — Message Video Task Handler implementation.

#include "api/MessageVideoTaskHandler.h"

#include <algorithm>

#include "service/algorithm/IAlgorithmQuery.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/task/ITaskQuery.h"
#include "util/ErrorCode.h"

namespace cosmo {

// Minimum number of action status entries required for a task to be included
// in the RunningDetail response (filters out partially-started tasks).
static constexpr size_t kMinActionStatusCount = 2;

// Default time window (seconds) for throughput statistics in RunningDetail.
static constexpr unsigned int kRunningDetailDurationSec = 60;

// Maximum page size for camera query when listing channels.
static constexpr int kMaxChannelQueryPageSize = 10000;

MessageVideoTaskHandler::MessageVideoTaskHandler(service::ICameraTaskConfig& task_config,
                                                 service::IAlgorithmQuery& algorithm_query,
                                                 service::ICameraDeviceCrud& camera_crud,
                                                 service::ITaskQuery& task_query)
    : task_config_(task_config),
      algorithm_query_(algorithm_query),
      camera_crud_(camera_crud),
      task_query_(task_query) {}

VideoTask::MsgModifyParamSend MessageVideoTaskHandler::Handle(VideoTask::MsgModifyParamRecv&& data,
                                                              std::error_condition& errc) {
    VideoTask::MsgModifyParamSend retData{};
    errc = task_config_.ModifyTaskParam(data.channelId, data.algorithmId, data.taskConfig);
    return retData;
}

VideoTask::MsgQueryParamSend MessageVideoTaskHandler::Handle(VideoTask::MsgQueryParamRecv&& data,
                                                             std::error_condition& errc) {
    VideoTask::MsgQueryParamSend retData{};
    errc = task_config_.QueryTaskParam(data.channelId, data.algorithmId, retData.resData.params);
    return retData;
}

VideoTask::MsgModifyAreaSend MessageVideoTaskHandler::Handle(VideoTask::MsgModifyAreaRecv&& data,
                                                             std::error_condition& errc) {
    VideoTask::MsgModifyAreaSend retData{};
    errc = task_config_.ModifyTaskArea(data.channelId, data.algorithmId, data.areas, data.shieldedAreas);
    return retData;
}

VideoTask::MsgQueryAreaSend MessageVideoTaskHandler::Handle(VideoTask::MsgQueryAreaRecv&& data,
                                                            std::error_condition& errc) {
    VideoTask::MsgQueryAreaSend retData{};
    errc = task_config_.QueryTaskArea(data.channelId, data.algorithmId, retData.resData.areas,
                                      retData.resData.shieldedAreas);
    return retData;
}

VideoTask::MsgModifyStrategySend MessageVideoTaskHandler::Handle(VideoTask::MsgModifyStrategyRecv&& data,
                                                                 std::error_condition& errc) {
    VideoTask::MsgModifyStrategySend retData{};
    errc = task_config_.ModifyTaskStrategy(data.channelId, data.algorithmId, data.scheduleId);
    return retData;
}

VideoTask::MsgQueryStrategySend MessageVideoTaskHandler::Handle(VideoTask::MsgQueryStrategyRecv&& data,
                                                                std::error_condition& errc) {
    VideoTask::MsgQueryStrategySend retData{};
    errc = task_config_.QueryTaskStrategy(data.channelId, data.algorithmId, retData.resData.scheduleId);
    return retData;
}

VideoTask::MsgSwitchTaskSend MessageVideoTaskHandler::Handle(VideoTask::MsgSwitchTaskRecv&& data,
                                                             std::error_condition& errc) {
    VideoTask::MsgSwitchTaskSend retData{};
    errc = task_config_.SwitchTask(data.channelId, data.algorithmId, data.enable);
    return retData;
}

VideoTask::MsgBatchSwitchTaskSend MessageVideoTaskHandler::Handle(VideoTask::MsgBatchSwitchTaskRecv&& data,
                                                                  std::error_condition& /*errc*/) {
    VideoTask::MsgBatchSwitchTaskSend retData{};
    for (auto& task : data.tasks) {
        std::error_condition ret = task_config_.SwitchTask(task.channelId, task.algorithmId, task.enable);
        if (util::ErrorEnum::Success != ret) {
            MsgResultInfo failedEl;
            failedEl.id      = task.id;
            failedEl.resCode = ret.value();
            failedEl.resMsg  = ret.message();
            retData.resData.failedList.push_back(failedEl);
        }
    }
    return retData;
}

VideoTask::MsgQuerySwitchSend MessageVideoTaskHandler::Handle(VideoTask::MsgQuerySwitchRecv&& data,
                                                              std::error_condition& errc) {
    VideoTask::MsgQuerySwitchSend retData{};
    bool enable            = false;
    errc                   = task_config_.QuerySwitch(data.channelId, data.algorithmId, enable);
    retData.resData.enable = static_cast<int>(enable);
    return retData;
}

VideoTask::MsgSaveOrUpdateSend MessageVideoTaskHandler::Handle(VideoTask::MsgSaveOrUpdateRecv&& data,
                                                               std::error_condition& errc) {
    VideoTask::MsgSaveOrUpdateSend retData{};
    errc = task_config_.ModifyTaskParam(data.channelId, data.algorithmId, data.taskConfig);
    if (util::ErrorEnum::Success != errc) {
        return retData;
    }

    errc = task_config_.ModifyTaskStrategy(data.channelId, data.algorithmId, data.scheduleId);
    if (util::ErrorEnum::Success != errc) {
        return retData;
    }

    errc = task_config_.SwitchTask(data.channelId, data.algorithmId, true);
    return retData;
}

VideoTask::MsgSelectConfigByAlgorithmIdSend MessageVideoTaskHandler::Handle(
    VideoTask::MsgSelectConfigByAlgorithmIdRecv&& data, std::error_condition& /*errc*/) {
    VideoTask::MsgSelectConfigByAlgorithmIdSend retData{};

    retData.resData.algorithmMetadata = algorithm_query_.GetMetaData(data.algorithmId);

    task_config_.QueryTaskParam(data.channelId, data.algorithmId, retData.resData.taskConfig.params);
    task_config_.QueryTaskArea(data.channelId, data.algorithmId, retData.resData.taskConfig.areas,
                               retData.resData.taskConfig.shieldedAreas);
    task_config_.QueryTaskStrategy(data.channelId, data.algorithmId, retData.resData.scheduleId);
    bool enable = false;
    task_config_.QuerySwitch(data.channelId, data.algorithmId, enable);
    retData.resData.taskEnableStatus = enable ? 1 : 0;

    return retData;
}

VideoTask::MsgDeleteSend MessageVideoTaskHandler::Handle(VideoTask::MsgDeleteRecv&& data,
                                                         std::error_condition& errc) {
    VideoTask::MsgDeleteSend retData{};
    errc = task_config_.DeleteTask(data.channelId, data.algorithmId);
    return retData;
}

VideoTask::MsgBatchDeleteSend MessageVideoTaskHandler::Handle(VideoTask::MsgBatchDeleteRecv&& data,
                                                              std::error_condition& /*errc*/) {
    VideoTask::MsgBatchDeleteSend retData{};
    for (auto& task : data.tasks) {
        std::error_condition ret = task_config_.DeleteTask(task.channelId, task.algorithmId);
        if (util::ErrorEnum::Success != ret) {
            MsgResultInfo failedEl;
            failedEl.id      = task.id;
            failedEl.resCode = ret.value();
            failedEl.resMsg  = ret.message();
            retData.resData.failedList.push_back(failedEl);
        }
    }

    return retData;
}

namespace {
    /// Build a CategoryInfo entry from an algorithm record.
    VideoTask::SelectAllAlgorithmInfoCategoryInfo BuildCategoryAlgInfo(
        const service::algorithm::AlgorithmPacketInfo& alg) {
        VideoTask::SelectAllAlgorithmInfoCategoryInfo info;
        info.algorithmId       = alg.id;
        info.algorithmName     = alg.algorithmName;
        info.algorithmCode     = alg.algorithmCode;
        info.algorithmCategory = alg.algorithmCategory;
        return info;
    }
}  // namespace

VideoTask::MsgSelectAllAlgorithmInfoSend MessageVideoTaskHandler::Handle(
    VideoTask::MsgSelectAllAlgorithmInfoRecv&& data, std::error_condition& /*errc*/) {
    VideoTask::MsgSelectAllAlgorithmInfoSend retData{};

    auto camera_tasks = task_config_.GetTasks(data.channelId);
    for (auto& camera_task : camera_tasks) {
        retData.resData.algorithmIds.push_back(camera_task.algorithmCode);
    }

    size_t total = 0;
    auto algs    = algorithm_query_.Query("", "", "", "", "", 1, 1000, total);
    for (auto& alg : algs) {
        auto it = std::find_if(retData.resData.algorithmList.begin(), retData.resData.algorithmList.end(),
                               [&alg](const auto& retCategory) {
                                   return retCategory.algorithmCategory == alg.algorithmCategory;
                               });
        if (it != retData.resData.algorithmList.end()) {
            it->simpleAlgorithmInfos.push_back(BuildCategoryAlgInfo(alg));
        } else {
            VideoTask::SelectAllAlgorithmInfoCategory categoryInfo;
            categoryInfo.algorithmCategory = alg.algorithmCategory;
            categoryInfo.simpleAlgorithmInfos.push_back(BuildCategoryAlgInfo(alg));
            retData.resData.algorithmList.push_back(categoryInfo);
        }
    }

    return retData;
}

VideoTask::MsgListChannelSend MessageVideoTaskHandler::Handle(VideoTask::MsgListChannelRecv&& data,
                                                              std::error_condition& errc) {
    VideoTask::MsgListChannelSend retData{};
    size_t total = 0;
    auto cameras = camera_crud_.Query("", static_cast<int>(ChannelStatus::ChannelStatusAll), 1,
                                      kMaxChannelQueryPageSize, total);

    for (const auto& camera : cameras) {
        auto tasks = task_config_.GetTasks(camera.videoChannelId);
        auto it    = std::find_if(tasks.begin(), tasks.end(),
                                  [&data](const auto& task) { return task.algorithmCode == data.algorithmId; });
        if (it == tasks.end()) {
            continue;
        }

        VideoTask::MsgListChannelItem item;
        item.id          = camera.videoChannelId;
        item.channelId   = camera.videoChannelId;
        item.channelName = camera.channelName;
        retData.resData.push_back(std::move(item));
    }

    errc = util::ErrorEnum::Success;
    return retData;
}

VideoTask::MsgApplyParamsBatchSend MessageVideoTaskHandler::Handle(VideoTask::MsgApplyParamsBatchRecv&& data,
                                                                   std::error_condition& errc) {
    VideoTask::MsgApplyParamsBatchSend retData{};

    for (const auto& channelId : data.targetChannelIds) {
        auto params              = data.taskConfig;
        std::error_condition ret = task_config_.ModifyTaskParam(channelId, data.algorithmId, params);
        if (util::ErrorEnum::Success == ret) {
            ret = task_config_.ModifyTaskStrategy(channelId, data.algorithmId, data.scheduleId);
        }
        if (util::ErrorEnum::Success == ret) {
            ret = task_config_.SwitchTask(channelId, data.algorithmId, true);
        }
        if (util::ErrorEnum::Success != ret) {
            MsgResultInfo failedEl;
            failedEl.id      = channelId;
            failedEl.resCode = ret.value();
            failedEl.resMsg  = ret.message();
            retData.resData.failedList.push_back(std::move(failedEl));
        }
    }

    errc = util::ErrorEnum::Success;
    return retData;
}

namespace {
    /// Convert flow-layer duration info to API-layer DTO.
    AlgActionNodeDurationInfo ConvertDurationInfo(const util::DurationStatInfo& src) {
        AlgActionNodeDurationInfo info;
        info.name          = src.name;
        info.durationUs    = src.duration_ns / 1000;
        info.durationAvgUs = (src.count > 0) ? (info.durationUs / src.count) : 0;
        info.durationCount = src.count;
        info.durationMaxUs = src.duration_max_ns / 1000;
        info.durationMinUs = src.duration_min_ns / 1000;
        info.costMaxUs     = src.cost_max_ns / 1000;
        info.costMinUs     = src.cost_min_ns / 1000;
        return info;
    }

    /// Convert flow-layer TaskStatus to API-layer MsgTaskStatus.
    MsgTaskStatus ConvertTaskStatus(const TaskStatus& src) {
        MsgTaskStatus dst;
        dst.channelId        = src.channelId;
        dst.taskId           = src.taskId;
        dst.streamUrl        = src.streamUrl;
        dst.algorithmId      = src.algorithmId;
        dst.algorithmName    = src.algorithmName;
        dst.algorithmVersion = src.algorithmVersion;

        for (auto& queue_status : src.queStatus) {
            ActionStatus action;
            action.actionId           = queue_status.actionId;
            action.name               = queue_status.queueStatus.name;
            std::error_condition qerr = queue_status.actionStatus;
            action.statusCode         = std::to_string(qerr.value());
            action.statusDesc         = qerr.message();
            action.holdCount          = queue_status.queueStatus.holdCount;
            action.alarmCount         = queue_status.alarmCount;
            action.insertCount        = queue_status.queueStatus.insertCount;
            action.processCount       = queue_status.queueStatus.processCount;
            action.discardCount       = queue_status.queueStatus.discardCount;
            action.periodMs           = queue_status.queueStatus.periodMs;
            action.insertCountPeriod  = queue_status.queueStatus.insertCountPeriod;
            action.processCountPeriod = queue_status.queueStatus.processCountPeriod;
            action.discardCountPeriod = queue_status.queueStatus.discardCountPeriod;
            dst.actionStatus.push_back(action);

            for (auto& duration_info : queue_status.durationInfos) {
                dst.nodeDurationInfos.push_back(ConvertDurationInfo(duration_info));
            }
        }
        return dst;
    }
}  // namespace

VideoTask::MsgRunningDetailSend MessageVideoTaskHandler::Handle(VideoTask::MsgRunningDetailRecv&& data,
                                                                std::error_condition& /*errc*/) {
    VideoTask::MsgRunningDetailSend retData{};

    auto task_statuses = task_query_.GetTaskStatus(data.tasks, kRunningDetailDurationSec);
    for (auto& status_el : task_statuses) {
        auto task_status = ConvertTaskStatus(status_el);
        if (task_status.actionStatus.size() > kMinActionStatusCount) {
            retData.resData.status.push_back(std::move(task_status));
        }
    }

    return retData;
}
}  // namespace cosmo
