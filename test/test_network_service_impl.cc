#include <chrono>
#include <filesystem>

#include "catch_amalgamated.hpp"
#include "service/detail/ServiceRegistry.h"
#include "service/network/impl/NetworkServiceImpl.h"
#include "test_mock_services.h"
#include "trompeloeil.hpp"
#include "util/IRequestDispatcher.h"

using namespace cosmo::service;

namespace {

/// Lightweight stub dispatcher — satisfies IRequestDispatcher without any
/// ServiceRegistry dependencies.  Avoids pulling in the full ApiRouter
/// dependency tree.
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

TEST_CASE("NetworkServiceImpl: network config and core dependency test", "[network-service]") {
    std::string testBaseDir =
        "/tmp/cosmo_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);

    auto oldPath = std::filesystem::current_path();
    std::filesystem::current_path(testBaseDir);

    cosmo::test::MockServiceRegistry mocks;
    cosmo::path::OverrideRootPathForTest(testBaseDir, testBaseDir);

    NetworkServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); },
                           []() { return std::make_unique<StubDispatcher>(); });

    SECTION("MQTT Start call (StandAlone)") {
        ALLOW_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);

        MqttParam mqttP;
        mqttP.enable = true;
        mqttP.url    = "127.0.0.1";
        mqttP.port   = 1883;
        ALLOW_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqttP);

        ALLOW_CALL(mocks.deviceInfoSvc, GetDevSn()).RETURN("SN-12345");

        sut.MqttStart();

        REQUIRE(sut.IsMqttEnabled() == false);
        sut.MqttStop();
    }

    std::filesystem::current_path(oldPath);
    std::filesystem::remove_all(testBaseDir);
}
