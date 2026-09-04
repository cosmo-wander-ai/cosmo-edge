#include "catch_amalgamated.hpp"
/*
 * test_device_info_service_impl.cc — DeviceInfoServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: DeviceInfoServiceImpl reads hardware info at construction via
 * platform-specific APIs (bm_get_*, /proc/*). Full functional tests require
 * aarch64 device. We tag device-dependent tests with [.device].
 * Cross-platform tests focus on construction safety and basic getters.
 */
#include <algorithm>
#include <thread>

#include "mock/MockAppInfoService.h"
#include "mock/MockTaskService.h"
#include "service/system/impl/DeviceInfoServiceImpl.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;
using trompeloeil::_;

TEST_CASE("DeviceInfoServiceImpl: construction and destruction", "[DeviceInfoService][.device]") {
    REQUIRE_NOTHROW([]() { DeviceInfoServiceImpl sut; }());
}

TEST_CASE("DeviceInfoServiceImpl: GetDeviceInfo returns populated struct", "[DeviceInfoService][.device]") {
    cosmo::test::MockAppInfoService appInfoSvc;
    cosmo::test::ScopedServiceOverride<IAppInfoService> registration(appInfoSvc);
    ALLOW_CALL(appInfoSvc, GetAppRuntime()).RETURN(0);
    DeviceInfoServiceImpl sut;

    auto info = sut.GetDeviceInfo();
    // Software version should always be available
    REQUIRE(!info.softwareVersion.empty());
}

TEST_CASE("DeviceInfoServiceImpl: GetDevSn and GetDevModel", "[DeviceInfoService][.device]") {
    DeviceInfoServiceImpl sut;

    SECTION("GetDevModel returns non-empty") {
        auto model = sut.GetDevModel();
        REQUIRE(!model.empty());
    }
}

TEST_CASE("DeviceInfoServiceImpl: GetCpuUtilization returns valid range", "[DeviceInfoService][.device]") {
    DeviceInfoServiceImpl sut;

    // Give monitor thread time to poll
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto cpu = sut.GetCpuUtilization();
    REQUIRE(cpu >= 0.0);
    REQUIRE(cpu <= 1.0);
}

TEST_CASE("DeviceInfoServiceImpl: GetGpuNum returns at least 1", "[DeviceInfoService][.device]") {
    DeviceInfoServiceImpl sut;

    REQUIRE(sut.GetGpuNum() >= 1);
}

#ifdef COSMO_NN_USE_RKNN_BACKEND
TEST_CASE("DeviceInfoServiceImpl: RKNN shared memory is exposed once", "[DeviceInfoService][.device][rknn]") {
    cosmo::test::MockTaskService taskSvc;
    cosmo::test::ScopedServiceOverride<ITaskQuery> registration(taskSvc);
    ALLOW_CALL(taskSvc, PacketStatus(_, _, _, _)).LR_SIDE_EFFECT(_1 = 0; _2 = 0; _3 = 0; _4 = 0);
    DeviceInfoServiceImpl sut;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    double custom_score = 0.0;
    const auto items    = sut.GetHardwareResource(custom_score);
    const auto general  = std::find_if(
        items.begin(), items.end(), [](const auto& item) { return item.key == "generalMemoryUtilization"; });
    REQUIRE(general != items.end());
    CHECK(general->memoryDomain == "system");
    CHECK((general->usedSize.find("GiB") != std::string::npos ||
           general->usedSize.find("MiB") != std::string::npos));

    const auto accelerator_memory_count = std::count_if(items.begin(), items.end(), [](const auto& item) {
        return item.key == "specialMemoryUtilization" || item.key == "modelMemoryUtilization" ||
               item.key == "pictureMemoryUtilization" || item.key == "TPPMemoryUtilization";
    });
    CHECK(accelerator_memory_count == 0);
    CHECK(sut.GetGpuUtilization().memoryDomain == "shared-system");
}
#endif
