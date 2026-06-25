// Unit tests for MessageSystemHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageSystemHandler.h"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockConfigWriteService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockSystemOperationService.h"
#include "mock/MockTimeService.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

MessageSystemHandler MakeHandler(MockServiceRegistry& mocks) {
    return MessageSystemHandler(mocks.configReadSvc, mocks.configWriteSvc, mocks.configNetSvc,
                                mocks.deviceInfoSvc, mocks.systemOpSvc, mocks.timeSvc);
}

}  // namespace

TEST_CASE("SystemHandler: QueryDeviceInfo", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    service::DeviceBasicInfo info;
    info.devSn      = "SN-TEST-001";
    info.devModel   = "COSMO-X";
    info.devVersion = "1.0.0";
    REQUIRE_CALL(mocks.deviceInfoSvc, GetDeviceInfo()).RETURN(info);

    System::MsgQueryDeviceInfoRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryPictureQuality", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgAlarmParamOverviewInfo quality;
    REQUIRE_CALL(mocks.configReadSvc, GetPictureQuality()).RETURN(quality);

    System::MsgQueryPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: SetPictureQuality", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetPictureQuality(_)).RETURN(cosmo::util::ErrorEnum::Success);

    System::MsgSetPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ResetPictureQuality", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, ResetPictureQuality()).RETURN(cosmo::util::ErrorEnum::Success);

    System::MsgResetPictureQualityRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryDebugMode", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configReadSvc, GetDebugMode()).RETURN(false);

    System::MsgQueryDebugModeRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: ModifyDebugMode", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configWriteSvc, SetDebugMode(true));

    System::MsgModifyDebugModeRecv data{};
    data.debugModeOpen = true;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryAlarmVideoDuration", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgAlarmParamVideoRecordInfo duration;
    REQUIRE_CALL(mocks.configReadSvc, GetAlarmVideoDuration()).RETURN(duration);

    System::MsgQueryAlarmVideoDurationRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryDevRebootParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::CfgRebootParamInfo reboot;
    REQUIRE_CALL(mocks.configReadSvc, GetRebootParam()).RETURN(reboot);

    System::MsgQueryDevRestartParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QuerySystemLogo", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::SystemLogoInfo logo;
    REQUIRE_CALL(mocks.configReadSvc, GetSystemLogo()).RETURN(logo);

    System::MsgQuerySystemLogoRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryRunModeParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);

    System::MsgQueryRunModeParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryHttpInterfaceParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::HttpPushParam httpParam;
    REQUIRE_CALL(mocks.configNetSvc, GetHttpInterfaceParam()).RETURN(httpParam);

    System::MsgQueryHttpInterfaceParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("SystemHandler: QueryMqttAdapterParam", "[system-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    cosmo::service::MqttParam mqttParam;
    REQUIRE_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqttParam);

    System::MsgQueryMqttAdapterParamRecv data{};
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}
