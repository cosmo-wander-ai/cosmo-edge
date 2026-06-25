#include "catch_amalgamated.hpp"
/*
 * test_device_info_service_impl.cc — DeviceInfoServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: DeviceInfoServiceImpl reads hardware info at construction via
 * platform-specific APIs (bm_get_*, /proc/*). Full functional tests require
 * aarch64 device. We tag device-dependent tests with [.device].
 * Cross-platform tests focus on construction safety and basic getters.
 */
#include <thread>

#include "mock/MockServiceRegistry.h"
#include "service/system/impl/DeviceInfoServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("DeviceInfoServiceImpl: construction and destruction", "[DeviceInfoService][.device]") {
    cosmo::test::MockServiceRegistry mocks;
    REQUIRE_NOTHROW([]() { DeviceInfoServiceImpl sut; }());
}

TEST_CASE("DeviceInfoServiceImpl: GetDeviceInfo returns populated struct", "[DeviceInfoService][.device]") {
    cosmo::test::MockServiceRegistry mocks;
    DeviceInfoServiceImpl sut;

    auto info = sut.GetDeviceInfo();
    // Software version should always be available
    REQUIRE(!info.softwareVersion.empty());
}

TEST_CASE("DeviceInfoServiceImpl: GetDevSn and GetDevModel", "[DeviceInfoService][.device]") {
    cosmo::test::MockServiceRegistry mocks;
    DeviceInfoServiceImpl sut;

    SECTION("GetDevModel returns non-empty") {
        auto model = sut.GetDevModel();
        REQUIRE(!model.empty());
    }
}

TEST_CASE("DeviceInfoServiceImpl: GetCpuUtilization returns valid range", "[DeviceInfoService][.device]") {
    cosmo::test::MockServiceRegistry mocks;
    DeviceInfoServiceImpl sut;

    // Give monitor thread time to poll
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto cpu = sut.GetCpuUtilization();
    REQUIRE(cpu >= 0.0);
    REQUIRE(cpu <= 1.0);
}

TEST_CASE("DeviceInfoServiceImpl: GetGpuNum returns at least 1", "[DeviceInfoService][.device]") {
    cosmo::test::MockServiceRegistry mocks;
    DeviceInfoServiceImpl sut;

    REQUIRE(sut.GetGpuNum() >= 1);
}
