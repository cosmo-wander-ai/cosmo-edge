#include "catch_amalgamated.hpp"
// Unit tests for TimeServiceImpl — validates NTP config management,
// timezone application, and time status queries.
// Uses temp JSON config files to isolate from production config.

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "service/system/impl/TimeServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "util/PathUtil.h"
#include "util/TimeUtil.h"

namespace fs = std::filesystem;

namespace {

class ScopedTimeZone final {
public:
    ScopedTimeZone() {
        const char* value = std::getenv("TZ");
        if (value != nullptr) {
            had_value_ = true;
            value_     = value;
        }
    }

    ~ScopedTimeZone() {
        if (had_value_) {
            setenv("TZ", value_.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }

    ScopedTimeZone(const ScopedTimeZone&)            = delete;
    ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;

private:
    bool had_value_{false};
    std::string value_;
};

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
    ScopedTimeZone timeZone;
    cosmo::test::ScopedPathOverride pathOverride;

    explicit TimeServiceTestConfig(const std::string& base = "/tmp/cosmo_time_test")
        : dir(base), pathOverride(dir, dir) {
        fs::remove_all(dir);
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

}  // namespace

TEST_CASE("TimeServiceImpl: GetTimeStatus basic properties", "[time][service]") {
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
        ntpCfg.server   = "127.0.0.1";
        ntpCfg.port     = 456;
        ntpCfg.interval = 120;

        auto result = timeSvc.SyncNtp(ntpCfg, 75);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);

        // Verify NTP config updated in memory
        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.ntp.enable == 1);
        REQUIRE(status.ntp.server == "127.0.0.1");
        REQUIRE(status.ntp.port == 456);
        REQUIRE(status.ntp.interval == 120);
    }

    SECTION("SyncNtp updates timezone when city list is loaded") {
        cosmo::service::NtpConfig ntpCfg;
        ntpCfg.enable = 0;

        // Change to Tokyo (ID=81, +09:00)
        auto result = timeSvc.SyncNtp(ntpCfg, 81);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);

        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.timeZoneId == 81);
        REQUIRE(status.timeZoneValue == "+09:00");
    }
}

TEST_CASE("TimeServiceImpl: rejects invalid NTP configuration at service boundary", "[time][service]") {
    TimeServiceTestConfig cfg;
    cosmo::service::TimeServiceImpl timeSvc;

    cosmo::service::NtpConfig ntpCfg;
    ntpCfg.enable   = 1;
    ntpCfg.server   = "ntp.example;reboot";
    ntpCfg.port     = 123;
    ntpCfg.interval = 60;
    REQUIRE(timeSvc.SyncNtp(ntpCfg, 75) == cosmo::util::ErrorEnum::InvalidParam);

    ntpCfg.server = "ntp.example";
    ntpCfg.port   = 0;
    REQUIRE(timeSvc.SyncNtp(ntpCfg, 75) == cosmo::util::ErrorEnum::InvalidParam);

    ntpCfg.port     = 123;
    ntpCfg.interval = 1441;
    REQUIRE(timeSvc.SyncNtp(ntpCfg, 75) == cosmo::util::ErrorEnum::InvalidParam);

    ntpCfg.interval = 60;
    REQUIRE(timeSvc.SyncNtp(ntpCfg, 96) == cosmo::util::ErrorEnum::InvalidParam);
}

TEST_CASE("TimeServiceImpl: SetTime commits configuration only when the clock update succeeds",
          "[time][service]") {
    TimeServiceTestConfig cfg;

    cosmo::service::TimeServiceImpl timeSvc;

    SECTION("SetTime always disables NTP") {
        // First enable NTP via SyncNtp
        cosmo::service::NtpConfig ntpCfg;
        ntpCfg.enable   = 1;
        ntpCfg.server   = "127.0.0.1";
        ntpCfg.port     = 123;
        ntpCfg.interval = 60;
        timeSvc.SyncNtp(ntpCfg, 75);

        const auto timestamp = cosmo::util::GetMilliseconds();
        auto result          = timeSvc.SetTime(timestamp, 75);
        REQUIRE((result == cosmo::util::ErrorEnum::Success ||
                 result == cosmo::util::ErrorEnum::OperationNotSupport ||
                 result == cosmo::util::ErrorEnum::SysErr));

        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.ntp.enable == (result == cosmo::util::ErrorEnum::Success ? 0 : 1));
    }

    SECTION("SetTime rolls back timezone ID when the platform rejects the clock update") {
        const auto timestamp = cosmo::util::GetMilliseconds();
        auto result          = timeSvc.SetTime(timestamp, 81);
        REQUIRE((result == cosmo::util::ErrorEnum::Success ||
                 result == cosmo::util::ErrorEnum::OperationNotSupport ||
                 result == cosmo::util::ErrorEnum::SysErr));

        std::vector<cosmo::service::TimeZoneItem> zones;
        auto status = timeSvc.GetTimeStatus(zones);
        REQUIRE(status.timeZoneId == (result == cosmo::util::ErrorEnum::Success ? 81 : 75));
    }
}

TEST_CASE("TimeServiceImpl: persistence failure does not split runtime configuration",
          "[time][service][persistence]") {
    TimeServiceTestConfig cfg("/tmp/cosmo_time_persistence_test");
    cosmo::service::TimeServiceImpl timeSvc;

    const std::string old_environment = std::getenv("TZ") == nullptr ? "" : std::getenv("TZ");

    // A directory at the target pathname lets the NTP write succeed and forces
    // the subsequent atomic timezone rename to fail.
    const auto timezone_path = cfg.dir + "/conf/TimeZoneInfo.json";
    fs::remove(timezone_path);
    fs::create_directory(timezone_path);

    cosmo::service::NtpConfig ntpCfg;
    ntpCfg.enable   = 1;
    ntpCfg.server   = "127.0.0.1";
    ntpCfg.port     = 123;
    ntpCfg.interval = 60;
    REQUIRE(timeSvc.SyncNtp(ntpCfg, 81) == cosmo::util::ErrorEnum::SysErr);

    std::vector<cosmo::service::TimeZoneItem> zones;
    const auto status = timeSvc.GetTimeStatus(zones);
    REQUIRE(status.ntp.enable == 0);
    REQUIRE(status.ntp.server == "ntp.aliyun.com");
    REQUIRE(status.timeZoneId == 75);
    REQUIRE((std::getenv("TZ") == nullptr ? "" : std::getenv("TZ")) == old_environment);

    std::ifstream ntp_file(cfg.dir + "/conf/NTPStatusConfig.json");
    const auto persisted = nlohmann::json::parse(ntp_file);
    REQUIRE(persisted.at("enable") == 0);
    REQUIRE(persisted.at("ntpServer") == "ntp.aliyun.com");
}

TEST_CASE("TimeServiceImpl: stopping an NTP receive is prompt", "[time][service][ntp][lifecycle]") {
    TimeServiceTestConfig cfg("/tmp/cosmo_time_lifecycle_test");

    const int server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(server >= 0);

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    REQUIRE(bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

    socklen_t address_size = sizeof(address);
    REQUIRE(getsockname(server, reinterpret_cast<sockaddr*>(&address), &address_size) == 0);
    timeval receive_timeout{};
    receive_timeout.tv_sec = 2;
    REQUIRE(setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)) == 0);

    auto timeSvc = std::make_unique<cosmo::service::TimeServiceImpl>();
    cosmo::service::NtpConfig ntpCfg;
    ntpCfg.enable   = 1;
    ntpCfg.server   = "127.0.0.1";
    ntpCfg.port     = ntohs(address.sin_port);
    ntpCfg.interval = 60;
    REQUIRE(timeSvc->SyncNtp(ntpCfg, 75) == cosmo::util::ErrorEnum::Success);

    std::array<uint8_t, 64> request{};
    REQUIRE(recv(server, request.data(), request.size(), 0) == 48);

    const auto started = std::chrono::steady_clock::now();
    timeSvc.reset();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(elapsed < std::chrono::milliseconds(500));
    close(server);
}

TEST_CASE("TimeServiceImpl: invalid persisted configuration is sanitized", "[time][service][load]") {
    TimeServiceTestConfig cfg("/tmp/cosmo_time_invalid_load_test");
    WriteJsonFile(
        cfg.dir + "/conf/TimeZoneInfo.json",
        R"({"m_nTzWest":99,"m_nTZOffset":9999,"m_nTZCitys":999,"m_strContent":"bad","m_value":"bad"})");
    WriteJsonFile(cfg.dir + "/conf/NTPStatusConfig.json",
                  R"({"enable":7,"ntpServer":"bad;host","NTPPort":0,"interval":99999})");

    cosmo::service::TimeServiceImpl timeSvc;
    std::vector<cosmo::service::TimeZoneItem> zones;
    const auto status = timeSvc.GetTimeStatus(zones);
    REQUIRE(status.timeZoneId == 75);
    REQUIRE(status.timeZoneValue == "+08:00");
    REQUIRE(status.ntp.enable == 0);
    REQUIRE(status.ntp.server == "ntp.aliyun.com");
    REQUIRE(status.ntp.port == 123);
    REQUIRE(status.ntp.interval == 60);
}

TEST_CASE("TimeServiceImpl: Construction with missing config files", "[time][service]") {
    ScopedTimeZone time_zone;
    std::string emptyDir = "/tmp/cosmo_time_empty_test";
    fs::remove_all(emptyDir);
    cosmo::test::ScopedPathOverride path_override(emptyDir, emptyDir);
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
