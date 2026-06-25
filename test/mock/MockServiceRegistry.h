#pragma once
#include <memory>
#include <vector>
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

namespace cosmo::test {

class MockTaskService;
class MockCameraService;
class MockAlgorithmService;
class MockModelService;
class MockScheduleService;
class MockAuthService;
class MockAppInfoService;
class MockLiveStreamService;
class MockActionService;
class MockClientMessageService;
class MockNetworkService;
class MockAlarmPushService;
class MockAlarmRecordService;
class MockDbService;
class MockBodyLibService;
class MockPersonDaoService;
class MockDeviceDiscoveryService;
class MockPersonRecogDaoService;
class MockArticlesReidDaoService;
class MockSystemOperationService;
class MockAudioService;
class MockLinkageService;
class MockVideoFrameCodec;
class MockConfigWriteService;
class MockConfigReadService;
class MockConfigNetworkService;
class MockDeviceInfoService;
class MockTimeService;
class MockFaceLibService;

struct MockServiceRegistryImpl;

struct MockServiceRegistry {
    std::unique_ptr<MockServiceRegistryImpl> impl;
    std::vector<std::unique_ptr<trompeloeil::expectation>>& expectations;
    MockTaskService& taskSvc;
    MockCameraService& cameraSvc;
    MockAlgorithmService& algSvc;
    MockModelService& modelSvc;
    MockScheduleService& scheduleSvc;
    MockAuthService& authSvc;
    MockAppInfoService& appInfoSvc;
    MockLiveStreamService& liveStreamSvc;
    MockActionService& actionSvc;
    MockClientMessageService& clientMsgSvc;
    MockNetworkService& networkSvc;
    MockAlarmPushService& alarmPushSvc;
    MockAlarmRecordService& alarmRecordSvc;
    MockDbService& dbSvc;
    MockBodyLibService& bodyLibSvc;
    MockPersonDaoService& personDaoSvc;
    MockDeviceDiscoveryService& discoveryService;
    MockPersonRecogDaoService& personRecogDaoSvc;
    MockArticlesReidDaoService& articlesReidDaoSvc;
    MockSystemOperationService& systemOpSvc;
    MockAudioService& audioSvc;
    MockLinkageService& linkageSvc;
    MockVideoFrameCodec& videoCodecSvc;
    MockConfigWriteService& configWriteSvc;
    MockConfigReadService& configReadSvc;
    MockConfigNetworkService& configNetSvc;
    MockDeviceInfoService& deviceInfoSvc;
    MockTimeService& timeSvc;
    MockFaceLibService& faceLibSvc;

    MockServiceRegistry();
    ~MockServiceRegistry();
};

}  // namespace cosmo::test
