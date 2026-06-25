#include "catch_amalgamated.hpp"
/*
 * test_mqtt_lifecycle_service_impl.cc — MqttLifecycleServiceImpl unit tests
 *
 * Strategy: Test construction/destruction and state queries.
 * Actual MQTT connection requires a broker, so connection tests are minimal.
 */
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockDeviceInfoService.h"
#include "mock/MockServiceRegistry.h"
#include "service/network/impl/MqttLifecycleServiceImpl.h"
#include "util/IRequestDispatcher.h"

using namespace cosmo::service;

namespace {

class StubDispatcher : public cosmo::IRequestDispatcher {
public:
    bool SupportsRoute(const std::string& /*uri*/) override {
        return false;
    }
    bool DispatchRequest(const std::string& /*uri*/, const std::string& /*mtk*/, const std::string& /*body*/,
                         std::string& /*response*/) override {
        return false;
    }
};

}  // namespace

TEST_CASE("MqttLifecycleServiceImpl: construction and destruction", "[mqtt-lifecycle]") {
    REQUIRE_NOTHROW(
        []() { MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); }); }());
}

TEST_CASE("MqttLifecycleServiceImpl: initial state", "[mqtt-lifecycle]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });

    SECTION("IsMqttRegistered returns false initially") {
        REQUIRE(sut.IsMqttRegistered() == false);
    }

    SECTION("IsMqttEnabled returns false initially") {
        REQUIRE(sut.IsMqttEnabled() == false);
    }
}

TEST_CASE("MqttLifecycleServiceImpl: MqttStop without MqttStart is safe", "[mqtt-lifecycle]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.MqttStop());
}

TEST_CASE("MqttLifecycleServiceImpl: double MqttStop is safe", "[mqtt-lifecycle]") {
    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.MqttStop());
    REQUIRE_NOTHROW(sut.MqttStop());
}

TEST_CASE("MqttLifecycleServiceImpl: MqttStart and MqttStop lifecycle", "[mqtt-lifecycle]") {
    cosmo::test::MockServiceRegistry mocks;

    ALLOW_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);
    cosmo::service::MqttParam mqttP;
    mqttP.enable = false;  // Disabled — no actual connection
    ALLOW_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqttP);
    ALLOW_CALL(mocks.deviceInfoSvc, GetDevSn()).RETURN("SN-TEST");

    MqttLifecycleServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); });
    REQUIRE_NOTHROW(sut.MqttStart());
    REQUIRE_NOTHROW(sut.MqttStop());
}
