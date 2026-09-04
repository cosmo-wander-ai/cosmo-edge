#pragma once

#include "mock/MockAlarmRecordService.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockArticlesReidDaoService.h"
#include "mock/MockAudioService.h"
#include "mock/MockAuthService.h"
#include "mock/MockBodyLibService.h"
#include "mock/MockCameraService.h"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockConfigWriteService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockFaceLibService.h"
#include "mock/MockLinkageService.h"
#include "mock/MockLiveStreamService.h"
#include "mock/MockModelAuthorizationService.h"
#include "mock/MockModelService.h"
#include "mock/MockNetworkService.h"
#include "mock/MockOnboardingService.h"
#include "mock/MockPersonDaoService.h"
#include "mock/MockPersonRecogDaoService.h"
#include "mock/MockScheduleService.h"
#include "mock/MockSystemOperationService.h"
#include "mock/MockTaskService.h"
#include "mock/MockTimeService.h"
#include "mock/MockVideoFrameCodec.h"
#include "support/ScopedServiceOverride.h"

namespace cosmo::test {

/// Exact construction dependencies of ApiRouter. This fixture intentionally
/// has no default behavior and no path side effects.
struct ApiRouterTestDependencies {
    MockAuthService authSvc;
    MockNetworkService networkSvc;
    MockAlgorithmService algSvc;
    MockCameraService cameraSvc;
    MockTaskService taskSvc;
    MockModelService modelSvc;
    MockScheduleService scheduleSvc;
    MockAlarmRecordService alarmRecordSvc;
    MockConfigReadService configReadSvc;
    MockConfigWriteService configWriteSvc;
    MockConfigNetworkService configNetSvc;
    MockDeviceInfoService deviceInfoSvc;
    MockSystemOperationService systemOpSvc;
    MockTimeService timeSvc;
    MockModelAuthorizationService modelAuthorizationSvc;
    MockLiveStreamService liveStreamSvc;
    MockFaceLibService faceLibSvc;
    MockPersonDaoService personDaoSvc;
    MockVideoFrameCodec videoCodecSvc;
    MockPersonRecogDaoService personRecogDaoSvc;
    MockBodyLibService bodyLibSvc;
    MockArticlesReidDaoService articlesReidDaoSvc;
    MockAudioService audioSvc;
    MockLinkageService linkageSvc;
    MockOnboardingService onboardingSvc;

    ScopedServiceOverride<service::IAuthService> auth{authSvc};
    ScopedServiceOverride<service::INetworkService> network{networkSvc};
    ScopedServiceOverride<service::IAlgorithmQuery> algorithmQuery{algSvc};
    ScopedServiceOverride<service::IAlgorithmCrud> algorithmCrud{algSvc};
    ScopedServiceOverride<service::IAlgorithmLayout> algorithmLayout{algSvc};
    ScopedServiceOverride<service::ICameraDeviceCrud> cameraDeviceCrud{cameraSvc};
    ScopedServiceOverride<service::ICameraTaskConfig> cameraTaskConfig{cameraSvc};
    ScopedServiceOverride<service::ICameraChannelQuery> cameraChannelQuery{cameraSvc};
    ScopedServiceOverride<service::ITaskQuery> taskQuery{taskSvc};
    ScopedServiceOverride<service::IModelService> model{modelSvc};
    ScopedServiceOverride<service::IScheduleService> schedule{scheduleSvc};
    ScopedServiceOverride<service::IAlarmRecordService> alarmRecord{alarmRecordSvc};
    ScopedServiceOverride<service::IConfigReadService> configRead{configReadSvc};
    ScopedServiceOverride<service::IConfigWriteService> configWrite{configWriteSvc};
    ScopedServiceOverride<service::IConfigNetworkService> configNetwork{configNetSvc};
    ScopedServiceOverride<service::IDeviceInfoService> deviceInfo{deviceInfoSvc};
    ScopedServiceOverride<service::ISystemOperationService> systemOperation{systemOpSvc};
    ScopedServiceOverride<service::ITimeService> time{timeSvc};
    ScopedServiceOverride<service::IModelAuthorizationService> modelAuthorization{modelAuthorizationSvc};
    ScopedServiceOverride<service::ILiveStreamService> liveStream{liveStreamSvc};
    ScopedServiceOverride<service::IFaceLibRepo> faceLibRepo{faceLibSvc};
    ScopedServiceOverride<service::IPersonRepo> personRepo{faceLibSvc};
    ScopedServiceOverride<service::IFaceFeature> faceFeature{faceLibSvc};
    ScopedServiceOverride<service::IPersonDaoService> personDao{personDaoSvc};
    ScopedServiceOverride<service::IFaceLibService> faceLib{faceLibSvc};
    ScopedServiceOverride<service::IVideoFrameCodec> videoFrameCodec{videoCodecSvc};
    ScopedServiceOverride<service::IPersonRecogDaoService> personRecogDao{personRecogDaoSvc};
    ScopedServiceOverride<service::IBodyLibService> bodyLib{bodyLibSvc};
    ScopedServiceOverride<service::IArticlesReidDaoService> articlesReidDao{articlesReidDaoSvc};
    ScopedServiceOverride<service::IAudioService> audio{audioSvc};
    ScopedServiceOverride<service::ILinkageService> linkage{linkageSvc};
    ScopedServiceOverride<service::IOnboardingService> onboarding{onboardingSvc};
};

}  // namespace cosmo::test
