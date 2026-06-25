#include "mock/MockServiceRegistry.h"

#include "mock/MockActionService.h"
#include "mock/MockAlarmPushService.h"
#include "mock/MockAlarmRecordService.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockArticlesReidDaoService.h"
#include "mock/MockAudioService.h"
#include "mock/MockAuthService.h"
#include "mock/MockBodyLibService.h"
#include "mock/MockCameraService.h"
#include "mock/MockClientMessageService.h"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockConfigWriteService.h"
#include "mock/MockDbService.h"
#include "mock/MockDeviceDiscoveryService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockFaceLibService.h"
#include "mock/MockLinkageService.h"
#include "mock/MockLiveStreamService.h"
#include "mock/MockModelService.h"
#include "mock/MockNetworkService.h"
#include "mock/MockPersonDaoService.h"
#include "mock/MockPersonRecogDaoService.h"
#include "mock/MockScheduleService.h"
#include "mock/MockSystemOperationService.h"
#include "mock/MockTaskService.h"
#include "mock/MockTimeService.h"
#include "mock/MockVideoFrameCodec.h"
#include "service/detail/ServiceRegistry.h"

namespace cosmo::test {

struct MockServiceRegistryImpl {
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
};

MockServiceRegistry::MockServiceRegistry()
    : impl(std::make_unique<MockServiceRegistryImpl>()),
      expectations(impl->expectations),
      taskSvc(impl->taskSvc),
      cameraSvc(impl->cameraSvc),
      algSvc(impl->algSvc),
      modelSvc(impl->modelSvc),
      scheduleSvc(impl->scheduleSvc),
      authSvc(impl->authSvc),
      appInfoSvc(impl->appInfoSvc),
      liveStreamSvc(impl->liveStreamSvc),
      actionSvc(impl->actionSvc),
      clientMsgSvc(impl->clientMsgSvc),
      networkSvc(impl->networkSvc),
      alarmPushSvc(impl->alarmPushSvc),
      alarmRecordSvc(impl->alarmRecordSvc),
      dbSvc(impl->dbSvc),
      bodyLibSvc(impl->bodyLibSvc),
      personDaoSvc(impl->personDaoSvc),
      discoveryService(impl->discoveryService),
      personRecogDaoSvc(impl->personRecogDaoSvc),
      articlesReidDaoSvc(impl->articlesReidDaoSvc),
      systemOpSvc(impl->systemOpSvc),
      audioSvc(impl->audioSvc),
      linkageSvc(impl->linkageSvc),
      videoCodecSvc(impl->videoCodecSvc),
      configWriteSvc(impl->configWriteSvc),
      configReadSvc(impl->configReadSvc),
      configNetSvc(impl->configNetSvc),
      deviceInfoSvc(impl->deviceInfoSvc),
      timeSvc(impl->timeSvc),
      faceLibSvc(impl->faceLibSvc) {
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
    expectations.push_back(
        NAMED_ALLOW_CALL(taskSvc, TaskCreate(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
            .RETURN(cosmo::util::ErrorEnum::Success));
    expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskChannelSetUrl(trompeloeil::_, trompeloeil::_)));
    expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskStop(trompeloeil::_)).RETURN(true));
    expectations.push_back(
        NAMED_ALLOW_CALL(taskSvc, TaskDelete(trompeloeil::_)).RETURN(cosmo::util::ErrorEnum::Success));
    expectations.push_back(
        NAMED_ALLOW_CALL(algSvc, GetAlgorithm(trompeloeil::_)).RETURN(std::make_shared<cosmo::ActionAlg>()));
    expectations.push_back(NAMED_ALLOW_CALL(algSvc, GetMetaData(trompeloeil::_)).RETURN("{}"));
    expectations.push_back(NAMED_ALLOW_CALL(scheduleSvc, GetDefaultId()).RETURN("default_sched"));
    expectations.push_back(
        NAMED_ALLOW_CALL(taskSvc, SetTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_)).RETURN(true));
    expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskIsStart(trompeloeil::_)).RETURN(false));
    expectations.push_back(NAMED_ALLOW_CALL(taskSvc, TaskStart(trompeloeil::_, trompeloeil::_)).RETURN(true));
    expectations.push_back(NAMED_ALLOW_CALL(taskSvc, RecordClearTaskData(trompeloeil::_)));
    expectations.push_back(NAMED_ALLOW_CALL(taskSvc, RecordTaskAction(trompeloeil::_, trompeloeil::_)));
}

MockServiceRegistry::~MockServiceRegistry() {
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

}  // namespace cosmo::test
