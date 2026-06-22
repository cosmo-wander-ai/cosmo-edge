// MessageHandler — Message router for dispatching HTTP/MQTT requests to appro...

#include "api/MessageHandler.h"

#include <unistd.h>

#include <filesystem>
#include <iomanip>

#include "service/algorithm/IActionService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/media/ILiveStreamService.h"
#include "service/media/IPicTaskService.h"
#include "service/network/IClientMessageService.h"
#include "service/system/IAppInfoService.h"
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskQuery.h"
#include "util/CipherUtil.h"
#include "util/ErrorCode.h"
#include "util/FileUtil.h"
#include "util/FormatString.h"
#include "util/JsonStructUtil.h"
#include "util/Keys.h"
#include "util/TimeUtil.h"

namespace cosmo {

// Test
MsgInterfaceTestSend MessageHandler::Handle(MsgInterfaceTestRecv&& data, std::error_condition& errc) {
    MsgInterfaceTestSend retData{};

    LOG_INFO("TEST:{}", data.test);

    if (data.test == "111") {
        errc = util::ErrorEnum::ParameterLenError;
    }

    return retData;
}

// Create task
MsgTaskCreateSend MessageHandler::Handle(MsgTaskCreateRecv&& data, std::error_condition& errc) {
    return service::ServiceRegistry::Instance().Get<service::ITaskLifecycle>().ProcessTaskCreate(data, errc);
}

// Delete task
MsgTaskCancleSend MessageHandler::Handle(MsgTaskCancleRecv&& data, std::error_condition& errc) {
    return service::ServiceRegistry::Instance().Get<service::ITaskLifecycle>().ProcessTaskCancel(data, errc);
}

// Create task
MsgPTaskCreateSend MessageHandler::Handle(MsgPTaskCreateRecv&& data, std::error_condition& errc) {
    return service::ServiceRegistry::Instance().Get<service::IPicTaskService>().ProcessPTaskCreate(data,
                                                                                                   errc);
}

// Delete task
MsgPTaskCancleSend MessageHandler::Handle(MsgPTaskCancleRecv&& data, std::error_condition& errc) {
    return service::ServiceRegistry::Instance().Get<service::IPicTaskService>().ProcessPTaskCancel(data,
                                                                                                   errc);
}

MsgOperateNodeSend MessageHandler::Handle(MsgOperateNodeRecv&& data, std::error_condition& /*errc*/) {
    MsgOperateNodeSend retData{};
    service::ServiceRegistry::Instance().Get<service::IClientMessageService>().NodeOperatorEventPush(data);
    return retData;
}

// Picture detection interface
MsgPTaskDetectPicSend MessageHandler::Handle(MsgPTaskDetectPicRecv&& data, std::error_condition& errc) {
    MsgPTaskDetectPicSend retData{};
    retData.resData.algorithmCode = data.algorithmCode;
    retData.resData.timestamp     = std::to_string(util::GetMilliseconds());
    if (data.taskId.empty()) {
        data.taskId = data.algorithmCode;
    }
    if (!IsTaskConfigEmpty(data.taskConfig))  // Set parameter if not empty
    {
        service::ServiceRegistry::Instance().Get<service::IPicTaskService>().SetTaskParam(data.taskId,
                                                                                          data.taskConfig);
    }

    errc = service::ServiceRegistry::Instance().Get<service::IPicTaskService>().DetectPic(data.taskId, data,
                                                                                          retData);
    return retData;
}

// China Mobile picture detection interface
MsgDetectSend MessageHandler::Handle(MsgDetectRecv&& data, std::error_condition& errc) {
    return service::ServiceRegistry::Instance().Get<service::IPicTaskService>().ProcessDetectGroup(data,
                                                                                                   errc);
}

// Info query
MsgInfoSend MessageHandler::Handle(MsgInfoRecv&& data, std::error_condition& errc) {
    return service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetSystemOverviewInfo(data,
                                                                                                      errc);
}

// Probe
MsgProbeSend MessageHandler::Handle(MsgProbeRecv&& /*data*/, std::error_condition& /*errc*/) {
    MsgProbeSend retData{};

    return retData;
}
// VRAM utilization
MsgGraphicsMemorySend MessageHandler::Handle(MsgGraphicsMemoryRecv&& data, std::error_condition& /*errc*/) {
    MsgGraphicsMemorySend retData{};
    retData.debugMessage =
        service::ServiceRegistry::Instance().Get<service::IAppInfoService>().OutputMallocBuf();

    LOG_INFO("GraphicsMemory receive messae is:{}", data.test);

    return retData;
}
// Test
MsgOverviewStructrueRecordSend MessageHandler::Handle(MsgOverviewStructrueRecordRecv&& data,
                                                      std::error_condition& /*errc*/) {
    MsgOverviewStructrueRecordSend retData{};

    service::ServiceRegistry::Instance().Get<service::IAppInfoService>().SetOverviewStructureRecord(
        data.functionSwitch);
    service::ServiceRegistry::Instance().Get<service::IAppInfoService>().SetOverviewStructureFile(
        data.functionSwitch);

    retData.path =
        service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetTaskOverviewDataPath();

    LOG_INFO("functionSwitch:{} path:{}", data.functionSwitch, retData.path);

    return retData;
}

MsgLoadLocalAlgorithmActionSend MessageHandler::Handle(MsgLoadLocalAlgorithmActionRecv&& data,
                                                       std::error_condition& errc) {
    MsgLoadLocalAlgorithmActionSend retData{};
    if (data.fileName.empty()) {
        if (data.taskActionType == MsgTaskActionType::MsgTaskActionTypePicture) {
            if (!service::ServiceRegistry::Instance().Get<service::IActionService>().UpdatePicActionAlg(
                    data.action)) {
                errc = util::ErrorEnum::ActionAlgLoadFailed;
            }
        } else {
            if (!service::ServiceRegistry::Instance().Get<service::IActionService>().UpdateActionAlg(
                    data.action)) {
                errc = util::ErrorEnum::ActionAlgLoadFailed;
            }
        }
    } else {
        auto fileNameAction =
            service::ServiceRegistry::Instance().Get<service::IAppInfoService>().UserDataPath() +
            data.fileName;
        auto algActionJson = util::ReadFile(fileNameAction);
        if (data.taskActionType == MsgTaskActionType::MsgTaskActionTypePicture) {
            if (!service::ServiceRegistry::Instance().Get<service::IActionService>().UpdatePicActionAlg(
                    data.action)) {
                errc = util::ErrorEnum::ActionAlgLoadFailed;
            }
        } else {
            if (!service::ServiceRegistry::Instance().Get<service::IActionService>().UpdateActionAlg(
                    algActionJson)) {
                errc = util::ErrorEnum::ActionAlgLoadFailed;
            }
        }
    }

    return retData;
}

// Logic result test
MsgLogicTestSend MessageHandler::Handle(MsgLogicTestRecv&& data, std::error_condition& /*errc*/) {
    MsgLogicTestSend retData{};

    retData.taskId      = data.taskId;
    retData.frame.index = data.frame.index;
    for (auto& target : data.frame.targets) {
        MsgTarget resPTarget = target;
        resPTarget.bLogicResult =
            service::ServiceRegistry::Instance().Get<service::ITaskLifecycle>().LogicTest(data.taskId,
                                                                                          target);
        retData.frame.targets.push_back(resPTarget);
    }

    return retData;
}

// Get structured file
MsgQueryTaskOverviewFileSend MessageHandler::Handle(MsgQueryTaskOverviewFileRecv&& data,
                                                    std::error_condition& /*errc*/) {
    MsgQueryTaskOverviewFileSend retData{};

    // Live stream or VOD stream
    bool is_live_stream = false;
    LOG_INFO("data.taskId:{}", data.taskId);
    service::ServiceRegistry::Instance().Get<service::ITaskQuery>().GetTaskFrameInfo(
        data.taskId, is_live_stream, retData.index, retData.pts, retData.frameSize, retData.streamUrl);

    if (is_live_stream) {
        retData.type = MsgTaskOverviewFileType::MsgTaskOverviewFileTypeLive;
        retData.liveDatas =
            service::ServiceRegistry::Instance().Get<service::ITaskQuery>().GetTaskLiveOverviewInfo(
                data.taskId);
    } else {
        auto path =
            service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetTaskOverviewDataPath();
        path = path + data.taskId;

        std::string filter;
        auto files = util::GetAllFileName(path, filter);
        LOG_INFO("Get File From :{} File Size:{}", path, files.size());
        for (auto& file : files) {
            MsgOverviewFile msgFile;
            msgFile.fileName      = file;
            auto content          = util::ReadFile((std::filesystem::path(path) / file).string());
            msgFile.base64Content = util::EncBase64(content);
            retData.files.push_back(msgFile);
        }
        retData.taskId = data.taskId;
        retData.type   = MsgTaskOverviewFileType::MsgTaskOverviewFileTypeVod;
    }

    return retData;
}

// Get task status
MsgQueryTaskStatusSend MessageHandler::Handle(MsgQueryTaskStatusRecv&& data, std::error_condition& /*errc*/) {
    MsgQueryTaskStatusSend retData{};

    auto taskStatuss =
        service::ServiceRegistry::Instance().Get<service::ITaskQuery>().GetTaskStatus(data.tasks, 60);
    for (auto& taskStatusEl : taskStatuss) {
        MsgTaskStatus taskStatus;
        taskStatus.channelId        = taskStatusEl.channelId;
        taskStatus.taskId           = taskStatusEl.taskId;
        taskStatus.streamUrl        = taskStatusEl.streamUrl;
        taskStatus.algorithmId      = taskStatusEl.algorithmId;
        taskStatus.algorithmName    = taskStatusEl.algorithmName;
        taskStatus.algorithmVersion = taskStatusEl.algorithmVersion;
        for (auto& queStatus : taskStatusEl.queStatus) {
            ActionStatus status;
            status.actionId         = queStatus.actionId;
            status.name             = queStatus.queueStatus.name;
            std::error_condition ec = queStatus.actionStatus;
            status.statusCode       = std::to_string(ec.value());
            status.statusDesc       = ec.message();

            status.holdCount          = queStatus.queueStatus.holdCount;
            status.alarmCount         = queStatus.alarmCount;
            status.insertCount        = queStatus.queueStatus.insertCount;
            status.processCount       = queStatus.queueStatus.processCount;
            status.discardCount       = queStatus.queueStatus.discardCount;
            status.periodMs           = queStatus.queueStatus.periodMs;
            status.insertCountPeriod  = queStatus.queueStatus.insertCountPeriod;
            status.processCountPeriod = queStatus.queueStatus.processCountPeriod;
            status.discardCountPeriod = queStatus.queueStatus.discardCountPeriod;
            taskStatus.actionStatus.push_back(status);
            for (auto& durationInfo : queStatus.durationInfos) {
                AlgActionNodeDurationInfo durationNodeInfo;
                durationNodeInfo.name       = durationInfo.name;
                durationNodeInfo.durationUs = durationInfo.duration_ns / 1000;
                if (durationInfo.count > 0)
                    durationNodeInfo.durationAvgUs = durationNodeInfo.durationUs / durationInfo.count;
                durationNodeInfo.durationCount = durationInfo.count;
                durationNodeInfo.durationMaxUs = durationInfo.duration_max_ns / 1000;
                durationNodeInfo.durationMinUs = durationInfo.duration_min_ns / 1000;
                durationNodeInfo.costMaxUs     = durationInfo.cost_max_ns / 1000;
                durationNodeInfo.costMinUs     = durationInfo.cost_min_ns / 1000;
                taskStatus.nodeDurationInfos.push_back(durationNodeInfo);
            }
        }

        if (taskStatus.actionStatus.size() > 2) {
            retData.status.push_back(taskStatus);
        }
    }

    return retData;
}

// Get task info
MsgQueryTaskInfoSend MessageHandler::Handle(MsgQueryTaskInfoRecv&& data, std::error_condition& /*errc*/) {
    MsgQueryTaskInfoSend retData{};

    if (data.taskId.empty()) {
        return retData;
    }

    bool is_live_stream = false;
    int64_t index;
    int64_t pts;
    int64_t frameSize;
    service::ServiceRegistry::Instance().Get<service::ITaskQuery>().GetTaskFrameInfo(
        data.taskId, is_live_stream, index, pts, frameSize, retData.streamUrl);

    auto path =
        service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetTaskOverviewDataPath();
    path = path + data.taskId;

    if (data.info) {
        auto content = util::ReadFile(path + "/taskInfo.json");
        (void)util::DecodeJson(content, retData.info);
    }

    if (data.action) {
        auto content = util::ReadFile(path + "/taskAction.json");
        (void)util::DecodeJson(content, retData.action);
    }

    return retData;
}

// Get device VRAM resource status details
MsgQueryDeviceMemStatusSend MessageHandler::Handle(MsgQueryDeviceMemStatusRecv&& /*data*/,
                                                   std::error_condition& /*errc*/) {
    MsgQueryDeviceMemStatusSend retData{};

    auto vram_statuses =
        service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetMemoryPoolStatus();
    for (auto& vram_pool : vram_statuses) {
        DeviceMemPoolStatus poolStatus;
        poolStatus.poolSize  = vram_pool.pool_size;
        poolStatus.mallocCnt = vram_pool.used_cnt;
        poolStatus.freeCnt   = vram_pool.idle_cnt;
        retData.totalMalloc += (vram_pool.used_cnt + vram_pool.idle_cnt) * vram_pool.pool_size;
        retData.totalInUsing += vram_pool.used_cnt * vram_pool.pool_size;
        for (auto& vram_node : vram_pool.used_nodes_status) {
            DeviceMemStatus node;
            node.threadId  = vram_node.thread_id;
            node.duration  = vram_node.duration;
            node.backtrace = {};
            poolStatus.mallocPoolStatus.push_back(node);
        }
        retData.status.push_back(poolStatus);
    }

    return retData;
}

MsgViewRoutesSend MessageHandler::Handle(MsgViewRoutesRecv&& data, std::error_condition& /*errc*/) {
    MsgViewRoutesSend retData{};

    service::ServiceRegistry::Instance().Get<service::ILiveStreamService>().SetViewCounts(data.viewCounts);
    return retData;
}

// Log list query
MsgQueryLogsSend MessageHandler::Handle(MsgQueryLogsRecv&& data, std::error_condition& errc) {
    return service::ServiceRegistry::Instance().Get<service::IAppInfoService>().GetPagedLogs(data, errc);
}
}  // namespace cosmo