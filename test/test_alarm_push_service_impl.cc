#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
/// @file test_alarm_push_service_impl.cc
/// @brief AlarmPushServiceImpl unit tests — validates config CRUD methods
///        (IsEnabled, GetUrl, SetPush) by pointing config path to a temp dir.
///
/// NOTE: Init() is NOT tested because it depends on IConfigReadService,
/// IEventNotifier, and PeriodicTimer (infrastructure-heavy).
/// The public config query/mutation API is fully testable in isolation.

#include <filesystem>
#include <fstream>

#include "mock/MockServiceRegistry.h"
#include "service/event/impl/AlarmPushServiceImpl.h"

namespace cosmo::test {

namespace {

    /// Creates a unique temporary directory for each test and returns its path.
    /// Call Setup() AFTER MockServiceRegistry construction.
    struct TempDir {
        std::string path;
        std::string cfgPath;
        TempDir() {
            static int counter = 0;
            path               = "/tmp/cosmo_test_alarm_" + std::to_string(++counter);
            cfgPath            = path + "/conf";
        }
        void Setup() {
            cosmo::path::OverrideRootPathForTest(path, path);
            std::filesystem::create_directories(cfgPath);
        }
        ~TempDir() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };

    /// Write a JSON config file compatible with nlohmann::json from_json.
    void WriteConfigFile(const std::string& cfgDir, bool bOpen, const std::string& url) {
        auto filePath = cfgDir + "/HttpAlarmPush.json";
        std::ofstream ofs(filePath);
        ofs << R"({"bOpen":)" << (bOpen ? "true" : "false") << R"(,"url":")" << url << R"("})";
        ofs.close();
    }

}  // namespace

TEST_CASE("AlarmPushService: default state when no config file exists", "[alarm-push]") {
    TempDir tmpDir;
    MockServiceRegistry mocks;
    tmpDir.Setup();

    service::AlarmPushServiceImpl sut;

    SECTION("IsEnabled returns false by default") {
        REQUIRE_FALSE(sut.IsEnabled());
    }

    SECTION("GetUrl returns empty string by default") {
        REQUIRE(sut.GetUrl().empty());
    }
}

TEST_CASE("AlarmPushService: loads existing config on construction", "[alarm-push]") {
    TempDir tmpDir;
    MockServiceRegistry mocks;
    tmpDir.Setup();
    WriteConfigFile(tmpDir.cfgPath, true, "http://example.com/alarm");

    // Verify the file was actually written before proceeding
    REQUIRE(std::filesystem::exists(tmpDir.cfgPath + "/HttpAlarmPush.json"));

    service::AlarmPushServiceImpl sut;

    SECTION("IsEnabled reflects config file") {
        REQUIRE(sut.IsEnabled());
    }

    SECTION("GetUrl reflects config file") {
        REQUIRE(sut.GetUrl() == "http://example.com/alarm");
    }
}

TEST_CASE("AlarmPushService: SetPush updates config", "[alarm-push]") {
    TempDir tmpDir;
    MockServiceRegistry mocks;
    tmpDir.Setup();

    service::AlarmPushServiceImpl sut;

    SECTION("Enable push with URL") {
        auto result = sut.SetPush(true, "http://new-server.com/push");
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.IsEnabled());
        REQUIRE(sut.GetUrl() == "http://new-server.com/push");
    }

    SECTION("Disable push preserves URL") {
        sut.SetPush(true, "http://test.com");
        sut.SetPush(false, "http://test.com");
        REQUIRE_FALSE(sut.IsEnabled());
        REQUIRE(sut.GetUrl() == "http://test.com");
    }

    SECTION("Change URL while enabled") {
        sut.SetPush(true, "http://old.com");
        sut.SetPush(true, "http://new.com");
        REQUIRE(sut.IsEnabled());
        REQUIRE(sut.GetUrl() == "http://new.com");
    }

    SECTION("SetPush persists config to file") {
        sut.SetPush(true, "http://persist-test.com");

        // Verify file was written
        auto cfgFile = tmpDir.cfgPath + "/HttpAlarmPush.json";
        REQUIRE(std::filesystem::exists(cfgFile));

        // Read back and verify content
        std::ifstream ifs(cfgFile);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        REQUIRE(content.find("persist-test.com") != std::string::npos);
        REQUIRE(content.find("true") != std::string::npos);
    }

    SECTION("Idempotent SetPush does not throw") {
        sut.SetPush(true, "http://test.com");
        auto result = sut.SetPush(true, "http://test.com");
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
    }
}

}  // namespace cosmo::test
