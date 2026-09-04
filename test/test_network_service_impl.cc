#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>

#include "catch_amalgamated.hpp"
#include "mock/MockConfigNetworkService.h"
#include "mock/MockConfigReadService.h"
#include "mock/MockDeviceInfoService.h"
#include "service/network/impl/NetworkServiceImpl.h"
#include "support/ScopedCurrentPath.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"
#include "trompeloeil.hpp"
#include "util/IRequestDispatcher.h"
#include "util/PathUtil.h"

using namespace cosmo::service;

namespace {

struct NetworkServiceDependencies {
    cosmo::test::MockConfigReadService configReadSvc;
    cosmo::test::MockConfigNetworkService configNetSvc;
    cosmo::test::MockDeviceInfoService deviceInfoSvc;
    cosmo::test::ScopedServiceOverride<IConfigReadService> configRead{configReadSvc};
    cosmo::test::ScopedServiceOverride<IConfigNetworkService> configNetwork{configNetSvc};
    cosmo::test::ScopedServiceOverride<IDeviceInfoService> deviceInfo{deviceInfoSvc};
};

class ClosedLoopbackPort final {
public:
    ClosedLoopbackPort() {
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            return;
        }

        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;
        if (bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return;
        }

        socklen_t length = sizeof(address);
        if (getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
            port_ = ntohs(address.sin_port);
        }
    }

    ~ClosedLoopbackPort() {
        if (socket_ >= 0) {
            close(socket_);
        }
    }

    [[nodiscard]] int Port() const {
        return port_;
    }

private:
    int socket_{-1};
    int port_{0};
};

class ScopedDirectoryRemoval final {
public:
    explicit ScopedDirectoryRemoval(std::filesystem::path path) : path_(std::move(path)) {}

    ~ScopedDirectoryRemoval() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

private:
    std::filesystem::path path_;
};

/// Lightweight stub dispatcher — satisfies IRequestDispatcher without any
/// ServiceRegistry dependencies.  Avoids pulling in the full ApiRouter
/// dependency tree.
class StubDispatcher : public cosmo::IRequestDispatcher {
public:
    bool SupportsRoute(const std::string& /*uri*/) override {
        return false;
    }
    cosmo::RequestAdmission InspectRequest(cosmo::RequestDispatchContext& /*context*/,
                                           bool /*require_known_route*/) override {
        return cosmo::RequestAdmission::kRouteNotFound;
    }
    bool DispatchRequest(const cosmo::RequestDispatchContext& /*context*/, const std::string& /*body*/,
                         std::string& /*response*/) override {
        return false;
    }
};

}  // namespace

TEST_CASE("NetworkServiceImpl: network config and core dependency test", "[network-service]") {
    std::string testBaseDir =
        "/tmp/cosmo_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);
    ScopedDirectoryRemoval cleanup(testBaseDir);

    cosmo::test::ScopedCurrentPath current_path(testBaseDir);
    cosmo::test::ScopedPathOverride path_override(testBaseDir, testBaseDir);
    NetworkServiceDependencies mocks;
    ClosedLoopbackPort closed_port;
    REQUIRE(closed_port.Port() > 0);

    NetworkServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); },
                           []() { return std::make_unique<StubDispatcher>(); });

    SECTION("MQTT Start call (StandAlone)") {
        ALLOW_CALL(mocks.configReadSvc, GetRunMode()).RETURN(cosmo::RunMode::RunModeStandAlone);

        MqttParam mqttP;
        mqttP.enable = true;
        mqttP.url    = "127.0.0.1";
        mqttP.port   = closed_port.Port();
        ALLOW_CALL(mocks.configNetSvc, GetMqttParam()).RETURN(mqttP);

        ALLOW_CALL(mocks.deviceInfoSvc, GetDevSn()).RETURN("SN-12345");

        sut.MqttStart();

        REQUIRE(sut.IsMqttEnabled() == false);
        sut.MqttStop();
    }
}

TEST_CASE("NetworkServiceImpl: HttpInit and Stop lifecycle", "[network-service]") {
    std::string testBaseDir =
        "/tmp/cosmo_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);
    ScopedDirectoryRemoval cleanup(testBaseDir);

    cosmo::test::ScopedCurrentPath current_path(testBaseDir);
    cosmo::test::ScopedPathOverride path_override(testBaseDir, testBaseDir);
    NetworkServiceDependencies mocks;

    NetworkServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); },
                           []() { return std::make_unique<StubDispatcher>(); });

    SECTION("StopHttpServer without Init is safe") {
        REQUIRE_NOTHROW(sut.RequestHttpStop());
        REQUIRE_NOTHROW(sut.StopHttpServer());
    }

    SECTION("Double StopHttpServer is safe") {
        REQUIRE_NOTHROW(sut.StopHttpServer());
        REQUIRE_NOTHROW(sut.StopHttpServer());
    }
}

TEST_CASE("NetworkServiceImpl: IsMqttRegistered and IsMqttEnabled initial state", "[network-service]") {
    std::string testBaseDir =
        "/tmp/cosmo_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);
    ScopedDirectoryRemoval cleanup(testBaseDir);

    cosmo::test::ScopedCurrentPath current_path(testBaseDir);
    cosmo::test::ScopedPathOverride path_override(testBaseDir, testBaseDir);
    NetworkServiceDependencies mocks;

    NetworkServiceImpl sut([]() { return std::make_unique<StubDispatcher>(); },
                           []() { return std::make_unique<StubDispatcher>(); });

    SECTION("IsMqttRegistered returns false before start") {
        REQUIRE(sut.IsMqttRegistered() == false);
    }

    SECTION("IsMqttEnabled returns false before start") {
        REQUIRE(sut.IsMqttEnabled() == false);
    }

    SECTION("MqttStop before MqttStart is safe") {
        REQUIRE_NOTHROW(sut.MqttStop());
    }

    SECTION("MqttShutdown is safe and idempotent") {
        REQUIRE_NOTHROW(sut.MqttShutdown());
        REQUIRE_NOTHROW(sut.MqttShutdown());
        REQUIRE_NOTHROW(sut.MqttStart());
    }

    SECTION("StopAsyncApply is safe and idempotent without an update") {
        REQUIRE_NOTHROW(sut.StopAsyncApply());
        REQUIRE_NOTHROW(sut.StopAsyncApply());
        cosmo::platform::NetCardInfo info;
        REQUIRE_NOTHROW(sut.ApplyCardInfoAsync(info));
    }
}
