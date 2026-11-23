#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <vector>

#include "catch_amalgamated.hpp"
#include "linkage/AlarmOutputController.h"
#include "platform/SystemReboot.h"
#include "platform/SystemTime.h"
#include "platform/WatchDog.h"
#include "service/system/impl/SystemOperationServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "util/PathUtil.h"
#include "util/RuntimeIsolation.h"
#include "util/UuidUtil.h"

namespace {

class TemporaryIsolationData {
public:
    TemporaryIsolationData()
        : path(std::filesystem::temp_directory_path() / ("cosmo-isolation-" + cosmo::util::GenerateUUID())) {
        std::filesystem::create_directories(path);
    }
    ~TemporaryIsolationData() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

class RecordingAlarmPin : public cosmo::linkage::AlarmOutputPin {
public:
    explicit RecordingAlarmPin(std::vector<bool>& writes) : writes_(writes) {}
    bool SetActive(bool active) override {
        writes_.push_back(active);
        return true;
    }

private:
    std::vector<bool>& writes_;
};

}  // namespace

TEST_CASE("Isolated runtime preserves factory-reset data and rejects upgrades", "[runtime-isolation]") {
    TemporaryIsolationData data;
    const auto sentinel = data.path / "keep.txt";
    std::ofstream(sentinel) << "retain validation evidence";

    cosmo::runtime::RuntimeIsolationScope isolated(true);
    const auto reset_error = cosmo::platform::ClearFactoryResetData(data.path.string());
    CHECK(reset_error == std::errc::operation_not_permitted);
    CHECK(std::filesystem::is_regular_file(sentinel));

    cosmo::service::SystemOperationServiceImpl service;
    CHECK(service.Upgrade((data.path / "absent-package.tar.gz").string()) ==
          cosmo::util::ErrorEnum::OperationNotSupport);
    CHECK(std::filesystem::is_regular_file(sentinel));
}

TEST_CASE("Isolated runtime refuses alarm pins before opening hardware", "[runtime-isolation]") {
    int opens = 0;
    std::vector<bool> writes;
    cosmo::linkage::AlarmOutputController controller({{1, 42, false}}, [&](const auto&) {
        ++opens;
        return std::make_unique<RecordingAlarmPin>(writes);
    });
    {
        cosmo::runtime::RuntimeIsolationScope isolated(true);
        CHECK_FALSE(controller.Pulse(1, std::chrono::seconds(5)));
        CHECK(opens == 0);
        CHECK(writes.empty());
    }
    // The ordinary mode still activates and resets the injected pin.
    REQUIRE(controller.Pulse(1, std::chrono::seconds(5)));
    controller.Stop();
    CHECK(opens == 1);
    CHECK(writes == std::vector<bool>{true, false});
}

TEST_CASE("Isolated runtime retains private log export", "[runtime-isolation]") {
    TemporaryIsolationData data;
    cosmo::test::ScopedPathOverride paths(data.path.string(), data.path.string());
    cosmo::runtime::RuntimeIsolationScope isolated(true);
    std::ofstream(std::filesystem::path(cosmo::path::GetLogPath()) / "candidate.log") << "isolated log";
    std::ofstream(std::filesystem::path(cosmo::path::GetCfgPath()) / "candidate.json") << "{}";

    cosmo::service::SystemOperationServiceImpl service;
    std::string name;
    std::string url;
    REQUIRE(service.ExportLogs(name, url) == cosmo::util::ErrorEnum::Success);
    CHECK_FALSE(name.empty());
    CHECK(std::filesystem::is_regular_file(data.path / std::filesystem::path(url).relative_path()));
}

// A regression in the isolation guard must never let this test arm a real
// watchdog or change a device clock. Device syscall tests use the wrapped driver
// harness; this CPU-only assertion catches the isolated Start return contract.
#if !defined(COSMO_NN_USE_SOPHON_BACKEND) && !defined(COSMO_NN_USE_RKNN_BACKEND)
TEST_CASE("Isolated CPU runtime refuses watchdog and clock operations", "[runtime-isolation]") {
    cosmo::runtime::RuntimeIsolationScope isolated(true);
    cosmo::platform::WatchDog watchdog;
    CHECK_FALSE(watchdog.Start());
    CHECK(watchdog.Stop());
    CHECK(cosmo::platform::SetSystemTime(0) == cosmo::util::ErrorEnum::OperationNotSupport);
}
#endif
