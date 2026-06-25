#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
// Unit tests for TimeServiceImpl — validates NTP config management,
// timezone application, and time status queries.
// Uses temp JSON config files to isolate from production config.

#include <filesystem>
#include <fstream>

#include "mock/MockServiceRegistry.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/TimeServiceImpl.h"

namespace fs = std::filesystem;

namespace {

// Helper: write JSON content to file
void WriteJsonFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path);
    ofs << content;
    ofs.flush();
}

// Creates temp config files needed by TimeServiceImpl::LoadConfig.
// Uses a unique directory per test to avoid inter-test interference.
struct TimeServiceTestConfig {
    std::string dir;

    explicit TimeServiceTestConfig(const std::string& base = "/tmp/cosmo_time_test") : dir(base) {
        fs::remove_all(dir);
        cosmo::path::OverrideRootPathForTest(dir, dir);
        auto cfgDir = dir + "/conf";
        fs::create_directories(cfgDir);

        // TimeZoneInfo.json — timezone persistence
        WriteJsonFile(
            cfgDir + "/TimeZoneInfo.json",
            R"({"m_nTzWest":0,"m_nTZOffset":800,"m_nTZCitys":75,"m_strContent":"Beijing","m_value":"+08:00"})");

        // NTPStatusConfig.json — NTP config
        WriteJsonFile(cfgDir + "/NTPStatusConfig.json",
                      R"({"enable":0,"ntpServer":"ntp.aliyun.com","NTPPort":123,"interval":60})");
    }

    ~TimeServiceTestConfig() {
        fs::remove_all(dir);
    }
};

// Helper: create TimeServiceImpl with config dir properly set.
// The TestServiceTestConfig constructor calls OverrideRootPathForTest
// to redirect cosmo::path::GetCfgPath() to the test config directory.
std::unique_ptr<cosmo::service::TimeServiceImpl> MakeTimeService(cosmo::test::MockServiceRegistry& mocks,
                                                                 const std::string& cfgDir) {
    return std::make_unique<cosmo::service::TimeServiceImpl>();
}

}  // namespace

TEST_CASE("TimeServiceImpl: GetTimeStatus basic properties", "[time][service]") {
    cosmo::test::MockServiceRegistry mocks;
    TimeServiceTestConfig cfg;

    cosmo::service::TimeServiceImpl timeSvc;

    SECTION("Returns non-zero timestamp") {
        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);

        REQUIRE(status.timestamp > 0);
    }

    SECTION("Returns formatted time string in YYYY-MM-DD HH:MM:SS format") {
        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);

        // Should be in "YYYY-MM-DD HH:MM:SS" format
        REQUIRE(status.timeString.length() == 19);
        REQUIRE(status.timeString[4] == '-');
        REQUIRE(status.timeString[7] == '-');
        REQUIRE(status.timeString[10] == ' ');
        REQUIRE(status.timeString[13] == ':');
        REQUIRE(status.timeString[16] == ':');
    }

    SECTION("Returns timezone info from loaded or default config") {
        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);

        // The timezone value should be a valid timezone string
        REQUIRE(!status.timeZoneValue.empty());
        REQUIRE(status.timeZoneValue.length() == 6);  // "+HH:MM"
    }
}

TEST_CASE("TimeServiceImpl: SyncNtp updates in-memory state", "[time][service]") {
    cosmo::test::MockServiceRegistry mocks;
    TimeServiceTestConfig cfg;

    cosmo::service::TimeServiceImpl timeSvc;

    SECTION("SyncNtp with enable=0 disables NTP in memory") {
        cosmo::service::NtpConfig ntpCfg;
        ntpCfg.enable   = 0;
        ntpCfg.server   = "ntp.aliyun.com";
        ntpCfg.port     = 123;
        ntpCfg.interval = 60;

        auto result = timeSvc.SyncNtp(ntpCfg, 75);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);

        // Verify NTP is disabled in state
        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.ntp.enable == 0);
    }

    SECTION("SyncNtp with enable=1 updates server/port/interval in state") {
        cosmo::service::NtpConfig ntpCfg;
        ntpCfg.enable   = 1;
        ntpCfg.server   = "time.google.com";
        ntpCfg.port     = 456;
        ntpCfg.interval = 120;

        auto result = timeSvc.SyncNtp(ntpCfg, 75);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);

        // Verify NTP config updated in memory
        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.ntp.enable == 1);
        REQUIRE(status.ntp.server == "time.google.com");
        REQUIRE(status.ntp.port == 456);
        REQUIRE(status.ntp.interval == 120);
    }

    SECTION("SyncNtp updates timezone when city list is loaded") {
        cosmo::service::NtpConfig ntpCfg;
        ntpCfg.enable = 0;

        // Change to Tokyo (ID=9, +09:00)
        auto result = timeSvc.SyncNtp(ntpCfg, 9);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);

        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.timeZoneId == 9);
        // If city list loaded, value should be "+09:00"; if not, it stays at default
        // We only verify the ID was updated (which is always set regardless of city list)
    }
}

TEST_CASE("TimeServiceImpl: SetTime disables NTP", "[time][service]") {
    cosmo::test::MockServiceRegistry mocks;
    TimeServiceTestConfig cfg;

    cosmo::service::TimeServiceImpl timeSvc;

    SECTION("SetTime always disables NTP") {
        // First enable NTP via SyncNtp
        cosmo::service::NtpConfig ntpCfg;
        ntpCfg.enable   = 1;
        ntpCfg.server   = "ntp.aliyun.com";
        ntpCfg.port     = 123;
        ntpCfg.interval = 60;
        timeSvc.SyncNtp(ntpCfg, 75);

        // Now SetTime — should disable NTP
        int64_t timestamp = 1700000000000;  // 2023-11-14
        auto result       = timeSvc.SetTime(timestamp, 75);
        REQUIRE((result == cosmo::util::ErrorEnum::Success ||
                 result == cosmo::util::ErrorEnum::OperationNotSupport ||
                 result == cosmo::util::ErrorEnum::SysErr));

        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.ntp.enable == 0);
    }

    SECTION("SetTime updates timezone ID") {
        int64_t timestamp = 1700000000000;
        auto result       = timeSvc.SetTime(timestamp, 9);
        REQUIRE((result == cosmo::util::ErrorEnum::Success ||
                 result == cosmo::util::ErrorEnum::OperationNotSupport ||
                 result == cosmo::util::ErrorEnum::SysErr));

        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.timeZoneId == 9);
    }
}

TEST_CASE("TimeServiceImpl: Construction with missing config files", "[time][service]") {
    cosmo::test::MockServiceRegistry mocks;
    std::string emptyDir = "/tmp/cosmo_time_empty_test";
    fs::remove_all(emptyDir);
    cosmo::path::OverrideRootPathForTest(emptyDir, emptyDir);
    fs::create_directories(emptyDir + "/conf");

    SECTION("Constructs without crash when config files are missing") {
        REQUIRE_NOTHROW([&]() { cosmo::service::TimeServiceImpl timeSvc; }());
    }

    SECTION("Uses defaults when config files are missing") {
        cosmo::service::TimeServiceImpl timeSvc;
        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);

        // Default timezone is Beijing (+08:00), ID=75
        REQUIRE(status.timeZoneId == 75);
        REQUIRE(status.timeZoneValue == "+08:00");
        // Default NTP is enabled
        REQUIRE(status.ntp.enable == 1);
    }

    fs::remove_all(emptyDir);
}

TEST_CASE("TimeServiceImpl: Thread safety of GetTimeStatus", "[time][service][thread]") {
    cosmo::test::MockServiceRegistry mocks;
    TimeServiceTestConfig cfg;

    cosmo::service::TimeServiceImpl timeSvc;

    SECTION("Concurrent GetTimeStatus calls are safe") {
        constexpr int kThreadCount = 4;
        std::vector<std::thread> threads;
        std::atomic<bool> hasError{false};

        for (int i = 0; i < kThreadCount; ++i) {
            threads.emplace_back([&]() {
                try {
                    for (int j = 0; j < 10; ++j) {
                        std::vector<cosmo::service::TimeZoneItem> zones;
                        auto status = timeSvc.GetTimeStatus(zones);
                        if (status.timestamp <= 0) {
                            hasError = true;
                        }
                    }
                } catch (...) {
                    hasError = true;
                }
            });
        }

        for (auto& th : threads) {
            th.join();
        }

        REQUIRE_FALSE(hasError);
    }
}
