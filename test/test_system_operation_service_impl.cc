#include <filesystem>
#include <fstream>

#include "catch_amalgamated.hpp"
#include "mock/MockServiceRegistry.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/PacketUpgrade.h"
#include "service/system/impl/SystemOperationServiceImpl.h"
#include "util/PathUtil.h"

namespace fs = std::filesystem;

TEST_CASE("SystemOperationServiceImpl: System operations", "[system][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::SystemOperationServiceImpl sysOpSvc;

    SECTION("ExportLogs creates tar file successfully") {
        std::string testRoot = "/tmp/cosmo_sysop_test";
        cosmo::path::OverrideRootPathForTest(testRoot, testRoot);

        // Create directories that cosmo::path:: will return
        auto webDir = cosmo::path::GetWebLocalPath();
        auto logDir = cosmo::path::GetLogPath();
        auto cfgDir = cosmo::path::GetCfgPath();

        // Create some dummy old logs to be cleaned up
        std::ofstream(webDir + "/IedLog_old.tar").put('a');
        std::ofstream(logDir + "/dummy.log").put('b');
        std::ofstream(cfgDir + "/dummy.cfg").put('c');

        std::string fileName;
        std::string fileUrl;
        auto result = sysOpSvc.ExportLogs(fileName, fileUrl);

        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(fileName.find("IedLog") == 0);
        REQUIRE(fileName.find(".tar") != std::string::npos);

        // Cleanup
        fs::remove_all(testRoot);
    }
}

TEST_CASE("PacketUpgrade accepts cosmo tar.gz package names", "[system][upgrade]") {
    std::string md5sum;

    auto result = cosmo::UpgradeFileNameCheck("cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.tar.gz", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::Success);
    REQUIRE(md5sum == "52d08574819464a735d4b0a90f26c924");

    result = cosmo::UpgradeFileNameCheck("cosmo-v1.1.0-52D08574819464A735D4B0A90F26C924.tar.gz", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::Success);
    REQUIRE(md5sum == "52d08574819464a735d4b0a90f26c924");

    result = cosmo::UpgradeFileNameCheck("cosmo-V1.1.0-52d08574819464a735d4b0a90f26c924.mpkt", md5sum);
    REQUIRE(result == cosmo::util::ErrorEnum::UpgradeFileVerifyFailed);
}

TEST_CASE("PacketUpgrade rejects empty filename", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade rejects random filename", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("random.txt", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("PacketUpgrade rejects missing md5", "[system][upgrade]") {
    std::string md5sum;
    auto result = cosmo::UpgradeFileNameCheck("cosmo-V1.0.0.tar.gz", md5sum);
    REQUIRE(result != cosmo::util::ErrorEnum::Success);
}

TEST_CASE("SystemOperationServiceImpl: ShowThreadDebugInfo does not crash", "[system][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::SystemOperationServiceImpl sysOpSvc;
    REQUIRE_NOTHROW(sysOpSvc.ShowThreadDebugInfo());
}
