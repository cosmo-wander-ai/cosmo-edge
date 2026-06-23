#pragma once

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/algorithm/IActionService.h"
#include "service/algorithm/IAlgorithmCrud.h"
#include "service/algorithm/IAlgorithmLayout.h"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/algorithm/IAlgorithmService.h"
#include "service/camera/ICameraChannelQuery.h"
#include "service/camera/ICameraDeviceCrud.h"
#include "service/camera/ICameraService.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/detail/ServiceRegistry.h"
#include "service/event/IAlarmPushService.h"
#include "service/event/IAlarmRecordService.h"
#include "service/face/IArticlesReidDaoService.h"
#include "service/face/IBodyLibService.h"
#include "service/face/IFaceLibService.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRecogDaoService.h"
#include "service/infra/IDbService.h"
#include "service/infra/ILinkageService.h"
#include "service/media/IAudioService.h"
#include "service/media/ILiveStreamService.h"
#include "service/media/IVideoFrameCodec.h"
#include "service/model/IModelService.h"
#include "service/network/IAuthService.h"
#include "service/network/IClientMessageService.h"
#include "service/network/IDeviceDiscoveryService.h"
#include "service/network/INetworkService.h"
#include "service/system/IAppInfoService.h"
#include "service/system/IConfigNetworkService.h"
#include "service/system/IConfigReadService.h"
#include "service/system/IConfigWriteService.h"
#include "service/system/IDeviceInfoService.h"
#include "service/system/IHardwareQuery.h"
#include "service/system/IMemoryDiag.h"
#include "service/system/IOverviewConfig.h"
#include "service/system/ISystemOperationService.h"
#include "service/system/ITimeService.h"
#include "service/task/IScheduleService.h"
#include "service/task/ITaskChannel.h"
#include "service/task/ITaskLifecycle.h"
#include "service/task/ITaskQuery.h"
#include "service/task/ITaskService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockTaskService : public cosmo::service::ITaskService {
public:
    // ITaskLifecycle
    MAKE_MOCK4(TaskCreate,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      cosmo::ActionAlgPtr),
               override);
    MAKE_MOCK1(TaskDelete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK2(ProcessTaskCreate, cosmo::MsgTaskCreateSend(cosmo::MsgTaskCreateRecv&, std::error_condition&),
               override);
    MAKE_MOCK2(ProcessTaskCancel, cosmo::MsgTaskCancleSend(cosmo::MsgTaskCancleRecv&, std::error_condition&),
               override);
    MAKE_MOCK2(TaskStart, bool(const std::string&, const std::string&), override);
    MAKE_MOCK1(TaskStop, bool(const std::string&), override);
    MAKE_MOCK1(TaskIsStart, bool(const std::string&), override);
    MAKE_MOCK3(SetTaskParam, bool(const std::string&, const std::string&, cosmo::MsgTaskConfig&), override);
    MAKE_MOCK2(LogicTest, bool(const std::string&, cosmo::MsgTarget&), override);
    MAKE_MOCK1(ShowActions, void(cosmo::ActionAlgPtr), override);
    MAKE_MOCK1(RecordClearTaskData, void(const std::string&), override);
    MAKE_MOCK2(RecordTaskInfo, void(const std::string&, cosmo::MsgTaskCreateRecv&), override);
    MAKE_MOCK2(RecordTaskAction, void(const std::string&, cosmo::ActionAlgPtr), override);

    // ITaskChannel
    MAKE_MOCK2(TaskChannelSetUrl, void(const std::string&, const std::string&), override);
    MAKE_MOCK3(TaskChannelSetParam, void(const std::string&, const std::string&, int), override);
    MAKE_MOCK2(CaptureImage, VideoFramePtr(const std::string&, int), override);
    int mock_dataStatus = 0;
    bool GetChannelAttr(const std::string& channelId, cosmo::MsgCameraAttr& attr) override {
        attr.dataStatus = mock_dataStatus;
        return true;
    }
    MAKE_MOCK1(TaskDataActive, bool(const std::string&), override);
    MAKE_MOCK1(GetChannelInst, cosmo::AlgChannelPtr(const std::string&), override);
    MAKE_MOCK1(GetChannelTasks, std::vector<std::string>(const std::string&), override);
    MAKE_MOCK2(GetAlarmInst, cosmo::TaskAlarmPtr(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetTaskChannel, std::string(const std::string&), override);
    MAKE_MOCK1(GetCameraInfo, void(std::vector<cosmo::MsgCameraInfo>&), override);

    // ITaskQuery
    MAKE_MOCK1(QueryTasks, std::vector<std::string>(bool), override);
    MAKE_MOCK3(GetTaskParam, bool(const std::string&, const std::string&, cosmo::MsgTaskConfig&), override);
    MAKE_MOCK2(GetTaskStatus, std::vector<cosmo::TaskStatus>(const std::vector<std::string>&, unsigned int),
               override);
    MAKE_MOCK0(CameraTaskInfo, std::vector<cosmo::MsgCameraInfo>(), override);
    MAKE_MOCK6(GetTaskFrameInfo, bool(const std::string&, bool&, int64_t&, int64_t&, int64_t&, std::string&),
               override);
    MAKE_MOCK0(TaskCount, size_t(), override);
    MAKE_MOCK1(GetAlgorithmCount, int(const std::string&), override);
    MAKE_MOCK2(QueueStatus, void(std::vector<cosmo::AlgActionDataQueueStatus>&, unsigned int), override);
    MAKE_MOCK2(QueueStatusDto, void(std::vector<cosmo::AlgActionDataQueueStatusDto>&, unsigned int),
               override);
    MAKE_MOCK4(PacketStatus, void(size_t&, size_t&, size_t&, size_t&), override);
    MAKE_MOCK4(GetTaskLiveOverviewInfo,
               std::vector<cosmo::MsgOverviewMem>(const std::string&, int64_t, int64_t, int64_t), override);
    MAKE_MOCK2(GetChannelAttr2, bool(const std::string&, cosmo::MsgCameraAttr&));
    // Note: GetChannelAttr satisfies both ITaskQuery and ITaskChannel base classes
    MAKE_MOCK5(GetTaskDetHistory,
               std::vector<cosmo::DataDetTrackClassify>(const std::string&, const std::string&, int64_t,
                                                        int64_t, int64_t),
               override);
    MAKE_MOCK2(GetTaskActionDurations,
               (std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>>)(const std::string&, int),
               override);
};

class MockCameraService : public cosmo::service::ICameraService {
public:
    MAKE_MOCK2(Add, cosmo::util::ErrorEnum(cosmo::MsgCameraInfo&, std::string&), override);
    MAKE_MOCK1(Update, cosmo::util::ErrorEnum(cosmo::MsgCameraInfo&), override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK5(Query, std::vector<cosmo::MsgCameraInfo>(const std::string&, int, int, int, size_t&),
               override);
    MAKE_MOCK3(ModifyTaskParam,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, cosmo::MsgTaskConfig&),
               override);
    MAKE_MOCK3(QueryTaskParam,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      std::vector<cosmo::MsgDynamicKeyValue>&),
               override);
    MAKE_MOCK4(ModifyTaskArea,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      const std::vector<cosmo::MsgTaskArea>&,
                                      const std::vector<cosmo::MsgTaskArea>&),
               override);
    MAKE_MOCK4(QueryTaskArea,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      std::vector<cosmo::MsgTaskArea>&, std::vector<cosmo::MsgTaskArea>&),
               override);
    MAKE_MOCK3(ModifyTaskStrategy,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&), override);
    MAKE_MOCK3(QueryTaskStrategy,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, std::string&), override);
    MAKE_MOCK3(SwitchTask, cosmo::util::ErrorEnum(const std::string&, const std::string&, bool), override);
    MAKE_MOCK3(QuerySwitch, cosmo::util::ErrorEnum(const std::string&, const std::string&, bool&), override);
    MAKE_MOCK2(DeleteTask, cosmo::util::ErrorEnum(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetTasks, std::vector<service::camera::CameraTaskDto>(const std::string&), override);
    MAKE_MOCK2(NotifyAlgorithmsChanged, void(const std::vector<std::string>&, bool), override);
    MAKE_MOCK1(NotifyAlgorithmsDeleted, void(const std::vector<std::string>&), override);
    MAKE_MOCK1(IsAlgorithmInUse, bool(const std::string&), const override);
    MAKE_MOCK1(ScheduleInUse, bool(const std::string&), override);
    MAKE_MOCK2(CaptureImage, VideoFramePtr(const std::string&, int), override);
    MAKE_MOCK4(BindTaskLibPara,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::vector<std::string>&,
                                      const std::string&),
               override);
    MAKE_MOCK0(IsIotNetworkMode, bool(), override);
    MAKE_MOCK1(EncodeJpeg, std::vector<uint8_t>(const VideoFramePtr&), override);
    MAKE_MOCK1(GetWebLocalPath, std::string(int64_t), override);
    MAKE_MOCK1(GetWebAccessPath, std::string(int64_t), override);
    MAKE_MOCK0(QueryUsbCameraList, std::vector<cosmo::camera::MsgUsbCameraDevice>(), override);
    MAKE_MOCK1(GetChannelInst, cosmo::AlgChannelPtr(const std::string&), override);
    MAKE_MOCK1(GetChannelName, std::string(const std::string&), const override);
    MAKE_MOCK0(InitCameraEntities, void(), override);
};

class MockAlgorithmService : public cosmo::service::IAlgorithmService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK1(Add, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK1(Add, cosmo::util::ErrorEnum(const cosmo::service::algorithm::AlgorithmPacketInfo&), override);
    MAKE_MOCK6(AddFromJson,
               cosmo::util::ErrorEnum(const std::string&, int, int, const std::string&, const std::string&,
                                      const std::string&),
               override);
    MAKE_MOCK1(LayoutSave, cosmo::util::ErrorEnum(const cosmo::service::algorithm::LayoutSaveReq&), override);
    MAKE_MOCK3(GetLayoutDetail,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      cosmo::service::algorithm::LayoutDetailResult&),
               override);
    MAKE_MOCK4(GetLayoutList,
               cosmo::util::ErrorEnum(const std::string&, int, const std::string&,
                                      cosmo::service::algorithm::LayoutListResult&),
               override);
    MAKE_MOCK6(LayoutExportSingle,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::string&,
                                      cosmo::service::algorithm::LayoutExportResult&),
               override);
    MAKE_MOCK6(LayoutExportAll,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::vector<std::string>&,
                                      cosmo::service::algorithm::LayoutExportResult&),
               override);
    MAKE_MOCK3(GetAtomicActionList,
               cosmo::util::ErrorEnum(int, const std::string&,
                                      cosmo::service::algorithm::AtomicActionListResult&),
               override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK4(Update,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, int, const std::string&),
               override);
    MAKE_MOCK8(Query,
               std::vector<cosmo::service::algorithm::AlgorithmPacketInfo>(
                   const std::string&, const std::string&, const std::string&, const std::string&,
                   const std::string&, int, int, size_t&),
               override);
    MAKE_MOCK1(GetAlgorithm, cosmo::ActionAlgPtr(const std::string&), override);
    MAKE_MOCK1(GetAlgorithmName, std::string(const std::string&), override);
    MAKE_MOCK1(GetMetaData, std::string(const std::string&), override);
    MAKE_MOCK1(GetAlgorithmsByModelId, std::vector<std::string>(const std::string&), override);
    MAKE_MOCK1(ReloadAlgorithmFromFile, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK0(GetAlgorithmPath, std::string(), override);
    MAKE_MOCK0(GetActionsJsonPath, std::string(), override);
    MAKE_MOCK0(GetPassFlowAlgorithms, std::vector<cosmo::service::algorithm::AlgorithmLocalInfo>(), override);
};

class MockModelService : public cosmo::service::IModelService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK0(GetModelPath, std::string(), override);
    MAKE_MOCK0(GetModelTemplatePath, std::string(), override);
    MAKE_MOCK0(GetModelComponentsJsonPath, std::string(), override);
    MAKE_MOCK1(ModelAdd, cosmo::util::ErrorEnum(const std::string&), override);

    MAKE_MOCK1(ModelValid, bool(const std::string&), override);
    MAKE_MOCK2(ModelValid, bool(const std::string&, std::string&), override);
    MAKE_MOCK5(QueryModelInfo,
               std::vector<cosmo::ModelInfo>(const std::string&, const std::string&, int, int, size_t&),
               override);
    MAKE_MOCK1(GetModelInfo, cosmo::ModelInfo(const std::string&), override);
    MAKE_MOCK7(UploadTempFile,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::string&, const std::string&,
                                      std::string&),
               override);
    MAKE_MOCK2(GetModelConfig, cosmo::util::ErrorEnum(const std::string&, std::string&), override);
    MAKE_MOCK2(SaveModelConfig, cosmo::util::ErrorEnum(const std::string&, const std::string&), override);
    MAKE_MOCK0(GetModelComponents, std::vector<cosmo::Model::MsgModelComponent>(), override);
    MAKE_MOCK1(DeleteModel, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK4(UpdateModel,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, int, const std::string&),
               override);
    MAKE_MOCK6(QueryModels,
               void(const std::string&, const std::string&, int, int, int&,
                    std::vector<cosmo::Model::MsgModel>&),
               override);
    MAKE_MOCK3(QueryAtomicModels,
               std::vector<cosmo::Model::MsgAtomicModel>(const std::string&, const std::string&,
                                                         const std::string&),
               override);
    MAKE_MOCK4(ExportModelConfig,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, std::string&, std::string&),
               override);
    MAKE_MOCK1(ImportModel, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK9(AddAtomicModel,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::vector<cosmo::Model::BmodelFileInfo>&,
                                      const std::string&, const std::string&, const std::string&,
                                      const std::string&),
               override);
    MAKE_MOCK2(SetModelPathMapping, void(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetModelPathMapping, std::string(const std::string&), override);
    MAKE_MOCK3(GetModelCfg, bool(const std::string&, std::string&, std::string&), override);
    MAKE_MOCK4(GetModelCfg, bool(const std::string&, std::string&, std::string&, std::string&), override);
};

class MockScheduleService : public cosmo::service::IScheduleService {
public:
    MAKE_MOCK2(Add, cosmo::util::ErrorEnum(cosmo::MsgScheduleTemplate&, std::string&), override);
    MAKE_MOCK1(Update, cosmo::util::ErrorEnum(cosmo::MsgScheduleTemplate&), override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK4(Query, std::vector<cosmo::MsgScheduleTemplate>(const std::string&, int, int, size_t&),
               override);
    MAKE_MOCK2(Exist2, bool(const std::string&, std::string&));
    bool Exist(const std::string& id, std::string& name) override {
        return Exist2(id, name);
    }

    MAKE_MOCK1(Exist1, bool(const std::string&));
    bool Exist(const std::string& id) override {
        return Exist1(id);
    }
    MAKE_MOCK1(InRunTime, bool(const std::string&), override);
    MAKE_MOCK0(GetDefaultId, std::string(), override);
};

class MockAuthService : public cosmo::service::IAuthService {
public:
    MAKE_MOCK2(Login,
               (std::pair<std::string, cosmo::util::ErrorEnum>)(const std::string&, const std::string&),
               override);
    MAKE_MOCK3(ChangePasswd,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&), override);
    MAKE_MOCK1(IsValidToken, bool(const std::string&), override);
};

// MockPathService removed — path accessors are now free functions in cosmo::path
// and isolated via cosmo::path::OverrideRootPathForTest() in MockServiceRegistry.

class MockAppInfoService : public cosmo::service::IAppInfoService {
public:
    MAKE_MOCK0(GetHaveManager, bool(), override);
    MAKE_MOCK0(GetEngineType, std::string(), override);
    MAKE_MOCK0(DevId, std::string(), override);
    MAKE_MOCK0(GetAppRuntime, int64_t(), override);
    MAKE_MOCK0(GetPicTaskGroupCount, int(), override);
    MAKE_MOCK0(UserDataPath, std::string(), override);
    MAKE_MOCK0(GetTaskOverviewDataPath, std::string(), override);
    MAKE_MOCK1(SetOverviewStructureRecord, void(bool), override);
    MAKE_MOCK0(GetOverviewStructureRecord, bool(), override);
    MAKE_MOCK1(SetOverviewStructureFile, void(bool), override);
    MAKE_MOCK0(GetOverviewStructureFile, bool(), override);
    MAKE_MOCK0(GetModelDebug, bool(), override);
    MAKE_MOCK0(GetNumber, size_t(), override);
    MAKE_MOCK0(LogPath, std::string(), override);
    MAKE_MOCK0(LogWebPath, std::string(), override);
    MAKE_MOCK0(GetCpuUtilization, double(), override);
    MAKE_MOCK0(GetGpuUtilization, cosmo::MsgGpuInfo(), override);
    MAKE_MOCK0(GetMemoryUtilization, cosmo::MsgMemoryInfo(), override);
    MAKE_MOCK0(GetDiskUtilization, cosmo::MsgDiskInfo(), override);
    MAKE_MOCK0(GetNetUtilization, cosmo::MsgNetInfo(), override);
    MAKE_MOCK0(GetAvailableGpuMemoryMB, int64_t(), override);
    MAKE_MOCK0(GetGpuNum, size_t(), override);
    MAKE_MOCK2(SetModelPath, void(const std::string&, const std::string&), override);
    MAKE_MOCK0(OutputMallocBuf, std::string(), override);
    MAKE_MOCK0(GetMemoryPoolStatus, std::vector<service::PoolStatusDto>(), override);
    MAKE_MOCK2(GetPagedLogs, cosmo::MsgQueryLogsSend(const cosmo::MsgQueryLogsRecv&, std::error_condition&),
               override);
    MAKE_MOCK2(GetSystemOverviewInfo, cosmo::MsgInfoSend(const cosmo::MsgInfoRecv&, std::error_condition&),
               override);
};

class MockLiveStreamService : public cosmo::service::ILiveStreamService {
public:
    MAKE_MOCK3(ViewerCreate,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      cosmo::LiveStream::LiveStreamInfo&),
               override);
    MAKE_MOCK2(ViewerDelete, bool(const std::string&, const std::string&), override);
    MAKE_MOCK2(ViewerHeartBeat, cosmo::util::ErrorEnum(const std::string&, const std::string&), override);
    MAKE_MOCK1(SetViewCounts, void(int), override);
};

class MockActionService : public cosmo::service::IActionService {
public:
    MAKE_MOCK2(GetActionAlg, cosmo::ActionAlgPtr(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetActionAlgByCode, cosmo::ActionAlgPtr(const std::string&), override);

    MAKE_MOCK1(UpdateActionAlg1, bool(std::string&));
    bool UpdateActionAlg(std::string& s) override {
        return UpdateActionAlg1(s);
    }

    MAKE_MOCK1(UpdateActionAlg2, bool(cosmo::ActionAlg&));
    bool UpdateActionAlg(cosmo::ActionAlg& a) override {
        return UpdateActionAlg2(a);
    }

    MAKE_MOCK2(GetPicActionAlg, cosmo::ActionAlgPtr(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetPicActionAlgByCode, cosmo::ActionAlgPtr(const std::string&), override);

    MAKE_MOCK1(UpdatePicActionAlg1, bool(std::string&));
    bool UpdatePicActionAlg(std::string& s) override {
        return UpdatePicActionAlg1(s);
    }

    MAKE_MOCK1(UpdatePicActionAlg2, bool(cosmo::ActionAlg&));
    bool UpdatePicActionAlg(cosmo::ActionAlg& a) override {
        return UpdatePicActionAlg2(a);
    }
};

class MockClientMessageService : public cosmo::service::IClientMessageService {
public:
    MAKE_MOCK2(FetchAlgorithmConfig,
               bool(cosmo::CMsgAlgorithmProcessConfigNGReq&, cosmo::CMsgAlgorithmProcessConfigNGRsp&),
               override);
    MAKE_MOCK2(FetchAtomicCodeList, bool(cosmo::CMsgGetAtomicCodeListReq&, cosmo::CMsgGetAtomicCodeListRsp&),
               override);
    MAKE_MOCK2(FetchVideoPlayUrl, bool(cosmo::CMsgGetVideoPlayReq&, cosmo::CMsgGetVideoPlayRsp&), override);
    MAKE_MOCK1(NodeOperatorEventPush, void(cosmo::MsgOperateNodeRecv&), override);
};

class MockNetworkService : public cosmo::service::INetworkService {
public:
    MAKE_MOCK0(IsMqttRegistered, bool(), override);
    MAKE_MOCK0(IsMqttEnabled, bool(), override);
    MAKE_MOCK0(MqttStop, void(), override);
    MAKE_MOCK0(MqttStart, void(), override);
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK1(GetCardRealInfo, cosmo::platform::NetCardInfo(bool), override);
    MAKE_MOCK0(GetCardRealInfos, std::vector<cosmo::platform::NetCardInfo>(), override);
    MAKE_MOCK1(SetCardInfo, bool(const cosmo::platform::NetCardInfo&), override);
    MAKE_MOCK1(ApplyCardInfoAsync, void(const cosmo::platform::NetCardInfo&), override);
    MAKE_MOCK0(GetCfgDns, std::vector<std::string>(), override);
    MAKE_MOCK1(SetDnss, bool(std::vector<std::string>), override);
    MAKE_MOCK3(SearchSetNewInfo, bool(cosmo::platform::NetCardInfo&, const std::string&, const std::string&),
               override);
    MAKE_MOCK0(GetNetCards, std::vector<cosmo::service::NetCardView>(), override);
    MAKE_MOCK0(GetHostIpAddress, std::string(), override);
    MAKE_MOCK2(InitHttpServer, void(const std::string&, uint16_t), override);
    MAKE_MOCK0(RunHttpLoop, void(), override);
    MAKE_MOCK0(StopHttpServer, void(), override);
    MAKE_MOCK2(ProbeNetworkQuality,
               cosmo::service::INetworkConfig::PingQualityResult(const std::string&, int), override);
    MAKE_MOCK1(IsIpAccessible, bool(const std::string&), override);
};

class MockAlarmRecordService : public cosmo::service::IAlarmRecordService {
public:
    MAKE_MOCK2(QueryEvents, std::vector<cosmo::MsgEventUnit>(cosmo::MsgConditionEvent&, int64_t&), override);
    MAKE_MOCK2(QueryAlarmRecords,
               cosmo::service::AlarmQueryResult(const cosmo::service::AlarmQueryCondition&, int), override);
    MAKE_MOCK1(QueryPassengerFlow, cosmo::service::FlowQueryResult(const cosmo::service::FlowQueryCondition&),
               override);
    MAKE_MOCK2(UpdateAlarmReportStatus, bool(const std::string&, bool), override);
    MAKE_MOCK1(Insert, bool(cosmo::AlarmRecordUnit&), override);
    MAKE_MOCK1(InsertFace, bool(cosmo::AlarmRecordUnit&), override);
    MAKE_MOCK2(QueryFace, std::vector<cosmo::MsgEventUnit>(cosmo::MsgConditionEvent&, int64_t&), override);
    MAKE_MOCK1(RemoveItems, void(const std::vector<std::string>&), override);
    MAKE_MOCK5(InsertPassFlow, bool(const std::string&, const std::string&, uint64_t, int, int), override);
};

class MockDbService : public cosmo::service::IDbService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK0(GetDb, std::shared_ptr<SQLite::Database>(), override);
};

class MockBodyLibService : public cosmo::service::IBodyLibService {
public:
    MAKE_MOCK5(BodyCompare,
               bool(const std::vector<std::string>&, const cosmo::AiFeature&,
                    cosmo::AiDetectMatchHighScoreInfo&, float, cosmo::service::CompareFeatureFunc),
               override);
    MAKE_MOCK1(InvalidateCache, void(const std::string&), override);
    MAKE_MOCK0(InvalidateAll, void(), override);
    MAKE_MOCK1(SetCacheTtlMs, void(int64_t), override);
    MAKE_MOCK1(ExtractBodyFeature, std::vector<float>(const VideoFramePtr&), override);
};

class MockPersonDaoService : public cosmo::service::IPersonDaoService {
public:
    MAKE_MOCK0(Begin, void(), override);
    MAKE_MOCK0(Commit, void(), override);
    MAKE_MOCK0(Rollback, void(), override);
    MAKE_MOCK1(AddFaceLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(UpdateFaceLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(RemoveFaceLib, bool(const std::string&), override);
    MAKE_MOCK1(ClearFaceLib, bool(const std::string&), override);
    MAKE_MOCK2(AddPerson,
               bool(const cosmo::db::PersonCondition&,
                    const std::vector<std::pair<std::string, std::vector<float>>>&),
               override);
    MAKE_MOCK1(AddPerson, bool(cosmo::db::FaceRegRecordUnit&), override);
    MAKE_MOCK2(UpdatePerson,
               bool(const cosmo::db::PersonCondition&,
                    const std::vector<std::pair<std::string, std::vector<float>>>&),
               override);
    MAKE_MOCK1(RemovePerson, bool(const std::string&), override);
    MAKE_MOCK1(QueryFaceLib, cosmo::db::FaceLibQueryResult(const cosmo::db::FaceLibQueryCondition&),
               override);
    MAKE_MOCK1(QueryPersons, cosmo::db::FacePersonQueryResult(const cosmo::db::FacePersonQueryCondition&),
               override);
    MAKE_MOCK1(QueryFaceFeature, std::vector<float>(const std::string&), override);
};

class MockDeviceDiscoveryService : public cosmo::service::IDeviceDiscoveryService {
public:
    MAKE_MOCK0(Start, void(), override);
    MAKE_MOCK0(Stop, void(), override);
};

class MockPersonRecogDaoService : public cosmo::service::IPersonRecogDaoService {
public:
    MAKE_MOCK0(Begin, void(), override);
    MAKE_MOCK0(Commit, void(), override);
    MAKE_MOCK0(Rollback, void(), override);
    MAKE_MOCK1(AddPersonLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(UpdatePersonLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(RemovePersonLib, bool(const std::string&), override);
    MAKE_MOCK1(ClearPersonLib, bool(const std::string&), override);
    MAKE_MOCK0(GetAllPersonLibs, std::vector<std::string>(), override);
    MAKE_MOCK4(AddPerson,
               bool(const std::string&, const std::string&, const std::string&, const std::vector<float>&),
               override);
    MAKE_MOCK1(RemovePerson, bool(const std::string&), override);
    MAKE_MOCK1(QueryPersonLib,
               cosmo::db::PersonRecogLibQueryResult(const cosmo::db::PersonRecogLibQueryCondition&),
               override);
    MAKE_MOCK1(QueryPersons, cosmo::db::PersonRecogQueryResult(const cosmo::db::PersonRecogQueryCondition&),
               override);
};

class MockArticlesReidDaoService : public cosmo::service::IArticlesReidDaoService {
public:
    MAKE_MOCK0(Begin, void(), override);
    MAKE_MOCK0(Commit, void(), override);
    MAKE_MOCK0(Rollback, void(), override);
    MAKE_MOCK1(AddArticlesReidLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(UpdateArticlesReidLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(RemoveArticlesReidLib, bool(const std::string&), override);
    MAKE_MOCK1(ClearArticlesReidLib, bool(const std::string&), override);
    MAKE_MOCK0(GetAllArticlesReidLibs, std::vector<std::string>(), override);
    MAKE_MOCK4(AddArticlesReid,
               bool(const std::string&, const std::string&, const std::string&, const std::vector<float>&),
               override);
    MAKE_MOCK1(RemoveArticlesReid, bool(const std::string&), override);
    MAKE_MOCK1(QueryThingsLib, cosmo::db::ThingsLibQueryResult(const cosmo::db::ThingsLibQueryCondition&),
               override);
    MAKE_MOCK1(QueryThings, cosmo::db::ThingsQueryResult(const cosmo::db::ThingsQueryCondition&), override);
};

class MockSystemOperationService : public cosmo::service::ISystemOperationService {
public:
    MAKE_MOCK1(RebootDevice, void(const std::string&), override);
    MAKE_MOCK1(ResetDevice, void(const std::string&), override);
    MAKE_MOCK2(ExportLogs, cosmo::util::ErrorEnum(std::string&, std::string&), override);
    MAKE_MOCK1(Upgrade, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK0(ShowThreadDebugInfo, void(), override);
};

class MockAlarmPushService : public cosmo::service::IAlarmPushService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK0(IsEnabled, bool(), override);
    MAKE_MOCK0(GetUrl, std::string(), override);
    MAKE_MOCK2(SetPush, cosmo::util::ErrorEnum(bool, const std::string&), override);
};

class MockAudioService : public cosmo::service::IAudioService {
public:
    MAKE_MOCK5(QueryAudioFiles,
               std::vector<cosmo::AlarmAudioInfo>(int&, const std::string&, const std::vector<std::string>&,
                                                  int, int),
               override);
    MAKE_MOCK1(GetAudioFileWebPath, std::string(cosmo::AlarmAudioInfo&), override);
    MAKE_MOCK1(GetAudioFileWebPath, std::string(const std::string&), override);
    MAKE_MOCK2(RemoveAudioFile, bool(const std::string&, std::string&), override);
    MAKE_MOCK1(AddAudioFile, bool(const std::string&), override);
    MAKE_CONST_MOCK0(AudioFileCount, size_t(), override);
    MAKE_CONST_MOCK0(AudioFileMaxCount, size_t(), override);
    MAKE_MOCK4(AddAudioDevice, bool(const std::string&, const std::string&, const std::string&, std::string&),
               override);
    MAKE_MOCK4(ModifyAudioDevice,
               bool(const std::string&, const std::string&, const std::string&, const std::string&),
               override);
    MAKE_MOCK2(RemoveAudioDevice, bool(const std::string&, std::string&), override);
    MAKE_MOCK4(QueryAudioDevices, std::vector<cosmo::AudioDeviceInfo>(int&, const std::string&, int, int),
               override);
    MAKE_MOCK1(CheckAudioDeviceAlive, bool(const std::string&), override);
    MAKE_MOCK1(PlayAudioDevice, bool(cosmo::AudioDevicePlay&), override);
};

class MockLinkageService : public cosmo::service::ILinkageService {
public:
    MAKE_MOCK3(Add, cosmo::util::ErrorEnum(const std::string&, const std::string&, std::string&), override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(std::string&), override);
    MAKE_MOCK3(Update, cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&),
               override);
    MAKE_MOCK4(Query, std::vector<cosmo::LinkageStrategyOutputUnit>(int, int, const std::string&, size_t&),
               override);
    MAKE_MOCK2(Switch, cosmo::util::ErrorEnum(std::string&, bool), override);
    MAKE_MOCK2(ReadSupportedStorage, bool(int&, std::vector<cosmo::StorageList>&), override);
    MAKE_MOCK1(IsAudioDeviceInUse, bool(const std::string&), override);
    MAKE_MOCK1(IsAudioFileInUse, bool(const std::string&), override);
    MAKE_MOCK2(Alarm, bool(const std::string&, const std::string&), override);
};

class MockConfigWriteService : public cosmo::service::IConfigWriteService {
public:
    MAKE_MOCK1(SetPictureQuality, cosmo::util::ErrorEnum(cosmo::CfgAlarmParamOverviewInfo), override);
    MAKE_MOCK0(ResetPictureQuality, cosmo::util::ErrorEnum(), override);
    MAKE_MOCK1(SetAlarmVideoDuration, cosmo::util::ErrorEnum(cosmo::CfgAlarmParamVideoRecordInfo), override);
    MAKE_MOCK0(ResetAlarmVideoDuration, cosmo::util::ErrorEnum(), override);
    MAKE_MOCK1(SetRebootParam, cosmo::util::ErrorEnum(cosmo::CfgRebootParamInfo), override);
    MAKE_MOCK0(ResetRebootParam, cosmo::util::ErrorEnum(), override);
    MAKE_MOCK4(SetSystemLogo,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::vector<uint8_t>&,
                                      const std::string&),
               override);
    MAKE_MOCK1(SetDebugMode, void(bool), override);
    MAKE_MOCK1(SetShieldedActions, void(const std::vector<std::string>&), override);
    MAKE_MOCK3(SetPopUpParam, void(int, int, int), override);
    MAKE_MOCK1(SetRunMode, void(cosmo::RunMode), override);
    MAKE_MOCK1(SetResourceLimit, void(bool), override);
};

class MockConfigReadService : public cosmo::service::IConfigReadService {
public:
    MAKE_MOCK0(GetPictureQuality, cosmo::CfgAlarmParamOverviewInfo(), override);
    MAKE_MOCK0(GetAlarmVideoDuration, cosmo::CfgAlarmParamVideoRecordInfo(), override);
    MAKE_MOCK0(GetRebootParam, cosmo::CfgRebootParamInfo(), override);
    MAKE_MOCK0(GetSystemLogo, cosmo::service::SystemLogoInfo(), override);
    MAKE_MOCK0(GetDebugMode, bool(), override);
    MAKE_MOCK0(GetShieldedActions, std::vector<std::string>(), override);
    MAKE_MOCK1(GetActionSwitch, bool(const std::string&), override);
    MAKE_MOCK3(GetPopUpParam, void(int&, int&, int&), override);
    MAKE_MOCK0(GetRunMode, cosmo::RunMode(), override);
    MAKE_MOCK0(GetResourceLimit, bool(), override);
    MAKE_MOCK0(IsNetworkModel, bool(), override);
};

class MockConfigNetworkService : public cosmo::service::IConfigNetworkService {
public:
    MAKE_MOCK0(GetHttpInterfaceParam, cosmo::service::HttpPushParam(), override);
    MAKE_MOCK1(SetHttpInterfaceParam, cosmo::util::ErrorEnum(const cosmo::service::HttpPushParam&), override);
    MAKE_MOCK0(GetMqttParam, cosmo::service::MqttParam(), override);
    MAKE_MOCK1(SetMqttParam, cosmo::util::ErrorEnum(const cosmo::service::MqttParam&), override);
    MAKE_MOCK0(GetIotNetworkParam, cosmo::service::IotNetworkParam(), override);
    MAKE_MOCK3(SetIotNetworkParam, void(const std::string&, const std::string&, int), override);
};

class MockDeviceInfoService : public cosmo::service::IDeviceInfoService {
public:
    MAKE_MOCK0(GetDeviceInfo, cosmo::service::DeviceBasicInfo(), override);
    MAKE_MOCK1(GetHardwareResource, std::vector<cosmo::service::HwResourceItem>(double&), override);
    MAKE_MOCK0(GetDevSn, std::string(), override);
    MAKE_MOCK0(GetDevModel, std::string(), override);
    MAKE_MOCK0(GetDevVersion, std::string(), override);
    MAKE_MOCK0(GetDevSpec, std::string(), override);
    MAKE_MOCK0(GetCpuUtilization, double(), override);
    MAKE_MOCK0(GetGpuUtilization, cosmo::MsgGpuInfo(), override);
    MAKE_MOCK0(GetMemoryUtilization, cosmo::MsgMemoryInfo(), override);
    MAKE_MOCK0(GetDiskUtilization, cosmo::MsgDiskInfo(), override);
    MAKE_MOCK0(GetNetUtilization, cosmo::MsgNetInfo(), override);
    MAKE_MOCK0(GetMacs, (std::vector<std::pair<std::string, std::string>>)(), override);
    MAKE_MOCK0(GetDevId, std::string(), override);
    MAKE_MOCK0(GetIPV4, std::string(), override);
    MAKE_MOCK0(GetAvailableGpuMemoryMB, int64_t(), override);
    MAKE_MOCK0(GetGpuNum, size_t(), override);
};

class MockTimeService : public cosmo::service::ITimeService {
public:
    MAKE_MOCK1(GetTimeStatus, cosmo::service::TimeStatus(std::vector<cosmo::service::TimeZoneItem>&),
               override);
    MAKE_MOCK2(SyncNtp, cosmo::util::ErrorEnum(const cosmo::service::NtpConfig&, int), override);
    MAKE_MOCK2(SetTime, cosmo::util::ErrorEnum(int64_t, int), override);
};

class MockFaceLibService : public cosmo::service::IFaceLibService {
public:
    // IFaceLibRepo
    MAKE_MOCK0(GetAllFaceLibs, std::vector<cosmo::FaceLibPtr>(), override);
    MAKE_CONST_MOCK0(GetFaceLibMaxCount, size_t(), override);
    MAKE_MOCK1(AddFaceLib, cosmo::util::ErrorEnum(cosmo::FaceLibPtr), override);
    MAKE_MOCK2(UpdateFaceLib, cosmo::util::ErrorEnum(const std::string&, cosmo::MsgBaseFaceLibInfo&&),
               override);
    MAKE_MOCK1(RemoveFaceLib, std::vector<cosmo::MsgResultFaceLibInfo>(const std::vector<std::string>&),
               override);
    MAKE_CONST_MOCK1(GetFaceLibs, std::vector<cosmo::FaceLibPtr>(std::vector<std::string>), override);
    MAKE_CONST_MOCK1(GetFaceLib, cosmo::FaceLibPtr(const std::string&), override);
    MAKE_MOCK2(CreateFaceLib, cosmo::FaceLibPtr(cosmo::MsgBaseFaceLibInfo&&, std::string&), override);

    // IPersonRepo
    MAKE_MOCK2(IsValidSerialNumber, bool(const std::string&, const std::string&), override);
    MAKE_CONST_MOCK1(GetPerson, cosmo::PersonPtr(const std::string&), override);
    MAKE_CONST_MOCK0(GetAllPerson, std::vector<cosmo::PersonPtr>(), override);
    MAKE_MOCK2(AddPerson, void(cosmo::FaceLibPtr, cosmo::PersonPtr), override);
    MAKE_MOCK2(UpdatePerson, void(std::vector<cosmo::FaceLibPtr>, cosmo::PersonPtr), override);
    MAKE_MOCK1(RemoveAllPerson, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK2(RemovePerson,
               std::vector<cosmo::MsgResultInfo>(cosmo::FaceLibPtr, const std::vector<std::string>&),
               override);
    MAKE_MOCK0(CreatePerson, cosmo::PersonPtr(), override);
    MAKE_CONST_MOCK1(GetPersonId, std::string(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK1(GetPersonPictureCount, size_t(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK1(GetPersonCreateTime, int64_t(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK1(GetPersonPictures, std::vector<cosmo::FacePicPtr>(const cosmo::PersonPtr&), override);
    MAKE_CONST_MOCK2(IsPersonInFaceLibs, bool(const cosmo::PersonPtr&, const std::vector<std::string>&),
                     override);
    MAKE_MOCK5(UpdatePersonMetadata,
               void(cosmo::PersonPtr, const std::string&, const std::string&, int64_t, int64_t), override);
    MAKE_MOCK2(AddPersonPicture, void(cosmo::PersonPtr, cosmo::FacePicPtr), override);
    MAKE_MOCK2(RemovePersonPicture, void(cosmo::PersonPtr, const std::string&), override);
    MAKE_MOCK3(CreateFacePic,
               cosmo::FacePicPtr(const std::string&, const std::string&, const cosmo::AiFeature&), override);

    // IFaceFeature
    MAKE_MOCK4(ExtractFaceFeature,
               cosmo::util::ErrorEnum(VideoFramePtr&, float, cosmo::AiFeature&, VideoFramePtr&), override);
    MAKE_MOCK2(CalculateFaceScore, float(const cosmo::AiFeature&, const cosmo::AiFeature&), override);
    MAKE_MOCK1(GetFaceScore, float(float), override);
    MAKE_MOCK4(FaceCompare,
               bool(std::vector<std::string>, cosmo::AiFeature&, cosmo::AiDetectMatchHighScoreInfo&, float),
               override);
    MAKE_MOCK0(LoadFaceData, void(), override);
    MAKE_MOCK0(ReleaseFaceModels, void(), override);

    // IFaceImport
    MAKE_MOCK2(ImportFile, void(const std::string&, const std::string&), override);
    MAKE_CONST_MOCK0(GetImportStatus, (std::pair<int, int>)(), override);
    MAKE_CONST_MOCK0(ImportComplete, bool(), override);
    MAKE_CONST_MOCK0(GetImportTotalCount, int(), override);
    MAKE_CONST_MOCK0(GetImportFailedUrl, std::string(), override);

    // IFaceLibService own method
    MAKE_MOCK1(SetQueryCond, std::string(const cosmo::MsgQueryFacesR&), override);
};

class MockVideoFrameCodec : public cosmo::service::IVideoFrameCodec {
public:
    MAKE_MOCK1(EncodeJpeg, std::vector<u_char>(const VideoFramePtr), override);
    MAKE_MOCK1(DecodeJpeg, VideoFramePtr(const std::vector<u_int8_t>&), override);
};

struct MockServiceRegistry {
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;
    MockTaskService taskSvc;
    MockCameraService cameraSvc;
    MockAlgorithmService algSvc;
    MockModelService modelSvc;
    MockScheduleService scheduleSvc;
    MockAuthService authSvc;

    MockAppInfoService appInfoSvc;
    MockLiveStreamService liveStreamSvc;
    MockActionService actionSvc;
    MockClientMessageService clientMsgSvc;
    MockNetworkService networkSvc;
    MockAlarmPushService alarmPushSvc;
    MockAlarmRecordService alarmRecordSvc;
    MockDbService dbSvc;
    MockBodyLibService bodyLibSvc;
    MockPersonDaoService personDaoSvc;
    MockDeviceDiscoveryService discoveryService;
    MockPersonRecogDaoService personRecogDaoSvc;
    MockArticlesReidDaoService articlesReidDaoSvc;
    MockSystemOperationService systemOpSvc;
    MockAudioService audioSvc;
    MockLinkageService linkageSvc;
    MockVideoFrameCodec videoCodecSvc;
    MockConfigWriteService configWriteSvc;
    MockConfigReadService configReadSvc;
    MockConfigNetworkService configNetSvc;
    MockDeviceInfoService deviceInfoSvc;
    MockTimeService timeSvc;
    MockFaceLibService faceLibSvc;

    MockServiceRegistry() {
        // Redirect all path accessors to /tmp to isolate tests from real filesystem
        cosmo::path::OverrideRootPathForTest("/tmp/cosmo_test", "/tmp/cosmo_test_app");
        service::ServiceRegistry::Instance().Set<service::ITaskService>(&taskSvc);
        // ISP sub-interface aliases for ITaskService
        service::ServiceRegistry::Instance().Set<service::ITaskLifecycle>(
            static_cast<service::ITaskLifecycle*>(&taskSvc));
        service::ServiceRegistry::Instance().Set<service::ITaskQuery>(
            static_cast<service::ITaskQuery*>(&taskSvc));
        service::ServiceRegistry::Instance().Set<service::ITaskChannel>(
            static_cast<service::ITaskChannel*>(&taskSvc));
        service::ServiceRegistry::Instance().Set<service::ICameraService>(&cameraSvc);
        // ISP sub-interface aliases for ICameraService
        service::ServiceRegistry::Instance().Set<service::ICameraDeviceCrud>(
            static_cast<service::ICameraDeviceCrud*>(&cameraSvc));
        service::ServiceRegistry::Instance().Set<service::ICameraTaskConfig>(
            static_cast<service::ICameraTaskConfig*>(&cameraSvc));
        service::ServiceRegistry::Instance().Set<service::ICameraChannelQuery>(
            static_cast<service::ICameraChannelQuery*>(&cameraSvc));
        service::ServiceRegistry::Instance().Set<service::IAlgorithmService>(&algSvc);
        // ISP sub-interface aliases for IAlgorithmService
        service::ServiceRegistry::Instance().Set<service::IAlgorithmQuery>(
            static_cast<service::IAlgorithmQuery*>(&algSvc));
        service::ServiceRegistry::Instance().Set<service::IAlgorithmCrud>(
            static_cast<service::IAlgorithmCrud*>(&algSvc));
        service::ServiceRegistry::Instance().Set<service::IAlgorithmLayout>(
            static_cast<service::IAlgorithmLayout*>(&algSvc));
        service::ServiceRegistry::Instance().Set<service::IModelService>(&modelSvc);
        service::ServiceRegistry::Instance().Set<service::IScheduleService>(&scheduleSvc);
        service::ServiceRegistry::Instance().Set<service::IAuthService>(&authSvc);

        service::ServiceRegistry::Instance().Set<service::IAppInfoService>(&appInfoSvc);
        // ISP sub-interface aliases for IAppInfoService
        service::ServiceRegistry::Instance().Set<service::IOverviewConfig>(
            static_cast<service::IOverviewConfig*>(&appInfoSvc));
        service::ServiceRegistry::Instance().Set<service::IHardwareQuery>(
            static_cast<service::IHardwareQuery*>(&appInfoSvc));
        service::ServiceRegistry::Instance().Set<service::IMemoryDiag>(
            static_cast<service::IMemoryDiag*>(&appInfoSvc));
        service::ServiceRegistry::Instance().Set<service::ILiveStreamService>(&liveStreamSvc);
        service::ServiceRegistry::Instance().Set<service::IActionService>(&actionSvc);
        service::ServiceRegistry::Instance().Set<service::IClientMessageService>(&clientMsgSvc);
        service::ServiceRegistry::Instance().Set<service::INetworkService>(&networkSvc);
        service::ServiceRegistry::Instance().Set<service::IAlarmPushService>(&alarmPushSvc);
        service::ServiceRegistry::Instance().Set<service::IAlarmRecordService>(&alarmRecordSvc);
        service::ServiceRegistry::Instance().Set<service::IDbService>(&dbSvc);
        service::ServiceRegistry::Instance().Set<service::IBodyLibService>(&bodyLibSvc);
        service::ServiceRegistry::Instance().Set<service::IPersonDaoService>(&personDaoSvc);
        service::ServiceRegistry::Instance().Set<service::IDeviceDiscoveryService>(&discoveryService);
        service::ServiceRegistry::Instance().Set<service::IPersonRecogDaoService>(&personRecogDaoSvc);
        service::ServiceRegistry::Instance().Set<service::IArticlesReidDaoService>(&articlesReidDaoSvc);
        service::ServiceRegistry::Instance().Set<service::ISystemOperationService>(&systemOpSvc);
        service::ServiceRegistry::Instance().Set<service::IAudioService>(&audioSvc);
        service::ServiceRegistry::Instance().Set<service::ILinkageService>(&linkageSvc);
        service::ServiceRegistry::Instance().Set<service::IVideoFrameCodec>(&videoCodecSvc);
        service::ServiceRegistry::Instance().Set<service::IConfigWriteService>(&configWriteSvc);
        service::ServiceRegistry::Instance().Set<service::IConfigReadService>(&configReadSvc);
        service::ServiceRegistry::Instance().Set<service::IConfigNetworkService>(&configNetSvc);
        service::ServiceRegistry::Instance().Set<service::IDeviceInfoService>(&deviceInfoSvc);
        service::ServiceRegistry::Instance().Set<service::ITimeService>(&timeSvc);
        service::ServiceRegistry::Instance().Set<service::IFaceLibService>(&faceLibSvc);
        service::ServiceRegistry::Instance().Set<service::IFaceLibRepo>(
            static_cast<service::IFaceLibRepo*>(&faceLibSvc));
        service::ServiceRegistry::Instance().Set<service::IPersonRepo>(
            static_cast<service::IPersonRepo*>(&faceLibSvc));
        service::ServiceRegistry::Instance().Set<service::IFaceFeature>(
            static_cast<service::IFaceFeature*>(&faceLibSvc));
        service::ServiceRegistry::Instance().Set<service::IFaceImport>(
            static_cast<service::IFaceImport*>(&faceLibSvc));

        // Keep expectations alive for the duration of the registry
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetNumber()).RETURN(1));
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetOverviewStructureRecord()).RETURN(false));
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetOverviewStructureFile()).RETURN(false));
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetModelDebug()).RETURN(false));
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetHaveManager()).RETURN(false));
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetAppRuntime()).RETURN(0));
        expectations.push_back(NAMED_ALLOW_CALL(configReadSvc, GetResourceLimit()).RETURN(false));
        expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskCreate(trompeloeil::_, trompeloeil::_,
                                                                    trompeloeil::_, trompeloeil::_))
                                   .RETURN(cosmo::util::ErrorEnum::Success));
        expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskChannelSetUrl(trompeloeil::_, trompeloeil::_)));
        expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskStop(trompeloeil::_)).RETURN(true));
        expectations.push_back(
            NAMED_ALLOW_CALL(taskSvc, TaskDelete(trompeloeil::_)).RETURN(cosmo::util::ErrorEnum::Success));
        expectations.push_back(NAMED_ALLOW_CALL(algSvc, GetAlgorithm(trompeloeil::_))
                                   .RETURN(std::make_shared<cosmo::ActionAlg>()));
        expectations.push_back(NAMED_ALLOW_CALL(algSvc, GetMetaData(trompeloeil::_)).RETURN("{}"));
        expectations.push_back(NAMED_ALLOW_CALL(scheduleSvc, GetDefaultId()).RETURN("default_sched"));
        expectations.push_back(
            NAMED_ALLOW_CALL(taskSvc, SetTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
                .RETURN(true));
        expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskIsStart(trompeloeil::_)).RETURN(false));
        expectations.push_back(
            NAMED_ALLOW_CALL(taskSvc, TaskStart(trompeloeil::_, trompeloeil::_)).RETURN(true));
        expectations.push_back(NAMED_ALLOW_CALL(taskSvc, RecordClearTaskData(trompeloeil::_)));
        expectations.push_back(NAMED_ALLOW_CALL(taskSvc, RecordTaskAction(trompeloeil::_, trompeloeil::_)));
    }

    ~MockServiceRegistry() {
        service::ServiceRegistry::Instance().Set<service::ITaskService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ITaskLifecycle>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ITaskQuery>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ITaskChannel>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ICameraService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ICameraDeviceCrud>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ICameraTaskConfig>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ICameraChannelQuery>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAlgorithmService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAlgorithmQuery>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAlgorithmCrud>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAlgorithmLayout>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IModelService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IScheduleService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAuthService>(nullptr);

        service::ServiceRegistry::Instance().Set<service::IAppInfoService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IOverviewConfig>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IHardwareQuery>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IMemoryDiag>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ILiveStreamService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IActionService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IClientMessageService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::INetworkService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAlarmPushService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAlarmRecordService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IDbService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IBodyLibService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IPersonDaoService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IDeviceDiscoveryService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IPersonRecogDaoService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IArticlesReidDaoService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ISystemOperationService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IAudioService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ILinkageService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IVideoFrameCodec>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IConfigWriteService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IConfigReadService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IConfigNetworkService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IDeviceInfoService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::ITimeService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IFaceLibService>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IFaceLibRepo>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IPersonRepo>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IFaceFeature>(nullptr);
        service::ServiceRegistry::Instance().Set<service::IFaceImport>(nullptr);
    }
};

}  // namespace cosmo::test
