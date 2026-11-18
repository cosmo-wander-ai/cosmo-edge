#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
#include "util/RuntimeIsolation.h"
/*
 * test_network_config_service_impl.cc — NetworkConfigServiceImpl unit tests
 *
 * Strategy: Test construction, DNS defaults, and network card query.
 * Full hardware-dependent tests (SetCardInfo, ApplyCardInfoAsync) are tagged [.device].
 */
#include <chrono>
#include <filesystem>

#include "mock/MockDeviceInfoService.h"
#include "service/network/impl/NetworkConfigServiceImpl.h"
#include "support/MockDefaults.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;

namespace {

struct NetworkConfigDependencies {
    cosmo::test::MockDeviceInfoService deviceInfoSvc;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::ScopedServiceOverride<IDeviceHardware> deviceInfo{deviceInfoSvc};

    NetworkConfigDependencies() {
        expectations.push_back(
            NAMED_ALLOW_CALL(deviceInfoSvc, GetMacs())
                .RETURN((std::vector<std::pair<std::string, std::string>>{{"eth0", "00:11:22:33:44:55"}})));
    }
};

}  // namespace

TEST_CASE("NetworkConfigServiceImpl: GetCfgDns returns default DNS", "[network-config]") {
    std::string testDir = "/tmp/cosmo_netcfg_test_" +
                          std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testDir);

    cosmo::test::ScopedPathOverride path_override(testDir, testDir);
    NetworkConfigDependencies mocks;

    NetworkConfigServiceImpl sut;
    auto dns = sut.GetCfgDns();
    // Default DNS should contain at least one entry
    REQUIRE(!dns.empty());

    std::filesystem::remove_all(testDir);
}

TEST_CASE("NetworkConfigServiceImpl: SetDnss and verify", "[network-config]") {
    std::string testDir = "/tmp/cosmo_netcfg_test_" +
                          std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testDir);

    cosmo::test::ScopedPathOverride path_override(testDir, testDir);
    NetworkConfigDependencies mocks;

    NetworkConfigServiceImpl sut;

    std::vector<std::string> newDns = {"8.8.8.8", "8.8.4.4"};
    auto result                     = sut.SetDnss(newDns);
    REQUIRE(result == true);

    auto dns = sut.GetCfgDns();
    REQUIRE(dns.size() >= 2);

    std::filesystem::remove_all(testDir);
}

TEST_CASE("Isolated network initialization stops before querying or applying hardware",
          "[network-config][runtime-isolation]") {
    cosmo::test::MockDeviceInfoService hardware;
    cosmo::test::ScopedServiceOverride<IDeviceHardware> registration(hardware);
    FORBID_CALL(hardware, GetMacs());
    const cosmo::runtime::RuntimeIsolationScope isolated(true);
    NetworkConfigServiceImpl service;
    REQUIRE_NOTHROW(service.Init());
}

#if !defined(COSMO_NN_USE_SOPHON_BACKEND) && !defined(COSMO_NN_USE_RKNN_BACKEND)
TEST_CASE("Isolated network writes fail without changing private configuration",
          "[network-config][runtime-isolation]") {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("cosmo_network_isolation_" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{directory};
    cosmo::test::ScopedPathOverride paths(directory.string(), directory.string());
    const cosmo::runtime::RuntimeIsolationScope isolated(true);
    NetworkConfigServiceImpl service;
    CHECK_FALSE(service.SetDnss({"192.0.2.1"}));
    cosmo::platform::NetCardInfo card;
    card.eth_name = "isolated-test-no-device";
    CHECK_FALSE(service.SetCardInfo(card));
    CHECK(std::filesystem::is_empty(directory));
}
#endif
