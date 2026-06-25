#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
// Unit tests for AppInfoServiceImpl — validates state management,
// delegation to DeviceInfoService, and thread-safe property accessors.

#include <thread>

#include "mock/MockDeviceInfoService.h"
#include "mock/MockServiceRegistry.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/AppInfoServiceImpl.h"

TEST_CASE("AppInfoServiceImpl: State management", "[appinfo][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::AppInfoServiceImpl appInfoSvc;

    SECTION("GetHaveManager returns false by default") {
        REQUIRE(appInfoSvc.GetHaveManager() == false);
    }

    SECTION("SetEngineType and GetEngineType round-trip") {
        appInfoSvc.SetEngineType("TestEngine");
        REQUIRE(appInfoSvc.GetEngineType() == "TestEngine");
    }

    SECTION("SetDevId and DevId round-trip") {
        appInfoSvc.SetDevId("DEV-001");
        REQUIRE(appInfoSvc.DevId() == "DEV-001");
    }

    SECTION("GetAppRuntime returns non-negative elapsed time") {
        auto runtime = appInfoSvc.GetAppRuntime();
        REQUIRE(runtime >= 0);
    }

    SECTION("GetPicTaskGroupCount returns default of 3") {
        REQUIRE(appInfoSvc.GetPicTaskGroupCount() == 3);
    }

    SECTION("OverviewStructureRecord toggle") {
        REQUIRE(appInfoSvc.GetOverviewStructureRecord() == false);
        appInfoSvc.SetOverviewStructureRecord(true);
        REQUIRE(appInfoSvc.GetOverviewStructureRecord() == true);
        appInfoSvc.SetOverviewStructureRecord(false);
        REQUIRE(appInfoSvc.GetOverviewStructureRecord() == false);
    }

    SECTION("OverviewStructureFile toggle") {
        REQUIRE(appInfoSvc.GetOverviewStructureFile() == false);
        appInfoSvc.SetOverviewStructureFile(true);
        REQUIRE(appInfoSvc.GetOverviewStructureFile() == true);
    }

    SECTION("GetModelDebug returns false by default") {
        REQUIRE(appInfoSvc.GetModelDebug() == false);
    }

    SECTION("GetNumber returns incrementing values") {
        auto n1 = appInfoSvc.GetNumber();
        auto n2 = appInfoSvc.GetNumber();
        auto n3 = appInfoSvc.GetNumber();
        REQUIRE(n2 == n1 + 1);
        REQUIRE(n3 == n2 + 1);
    }

    SECTION("LogWebPath returns fixed web path") {
        REQUIRE(appInfoSvc.LogWebPath() == "/logs/");
    }
}

TEST_CASE("AppInfoServiceImpl: Path delegation", "[appinfo][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::AppInfoServiceImpl appInfoSvc;

    SECTION("UserDataPath returns base dir + /cwai") {
        auto path = appInfoSvc.UserDataPath();
        // OverrideRootPathForTest sets /tmp/cosmo_test as base
        REQUIRE(path.find("/cwai") != std::string::npos);
    }

    SECTION("LogPath returns log dir + /logs/") {
        auto path = appInfoSvc.LogPath();
        REQUIRE(path.find("/log") != std::string::npos);
        REQUIRE(path.find("/logs/") != std::string::npos);
    }
}

TEST_CASE("AppInfoServiceImpl: HW utilization delegates to DeviceInfoService", "[appinfo][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::AppInfoServiceImpl appInfoSvc;

    SECTION("GetCpuUtilization delegates correctly") {
        REQUIRE_CALL(mocks.deviceInfoSvc, GetCpuUtilization()).RETURN(45.5);
        REQUIRE(appInfoSvc.GetCpuUtilization() == Catch::Approx(45.5));
    }

    SECTION("GetAvailableGpuMemoryMB delegates correctly") {
        REQUIRE_CALL(mocks.deviceInfoSvc, GetAvailableGpuMemoryMB()).RETURN(int64_t(4096));
        REQUIRE(appInfoSvc.GetAvailableGpuMemoryMB() == 4096);
    }

    SECTION("GetGpuNum delegates correctly") {
        REQUIRE_CALL(mocks.deviceInfoSvc, GetGpuNum()).RETURN(size_t(2));
        REQUIRE(appInfoSvc.GetGpuNum() == 2);
    }

    SECTION("GetMemoryUtilization delegates correctly") {
        cosmo::MsgMemoryInfo memInfo;
        memInfo.memtotal     = 16384;
        memInfo.memavailable = 8192;
        REQUIRE_CALL(mocks.deviceInfoSvc, GetMemoryUtilization()).RETURN(memInfo);

        auto result = appInfoSvc.GetMemoryUtilization();
        REQUIRE(result.memtotal == 16384);
        REQUIRE(result.memavailable == 8192);
    }
}

TEST_CASE("AppInfoServiceImpl: GetPagedLogs validation", "[appinfo][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::AppInfoServiceImpl appInfoSvc;

    SECTION("Invalid pageNum returns error") {
        cosmo::MsgQueryLogsRecv req;
        req.pageNum  = 0;
        req.pageSize = 10;
        std::error_condition errc;

        auto result = appInfoSvc.GetPagedLogs(req, errc);
        REQUIRE(errc == cosmo::util::ErrorEnum::ParameterException);
    }

    SECTION("Invalid pageSize returns error") {
        cosmo::MsgQueryLogsRecv req;
        req.pageNum  = 1;
        req.pageSize = 0;
        std::error_condition errc;

        auto result = appInfoSvc.GetPagedLogs(req, errc);
        REQUIRE(errc == cosmo::util::ErrorEnum::ParameterException);
    }
}

TEST_CASE("AppInfoServiceImpl: GetSystemOverviewInfo device validation", "[appinfo][service]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::AppInfoServiceImpl appInfoSvc;
    appInfoSvc.SetDevId("CORRECT-DEV-ID");

    SECTION("Mismatched devId returns InvalidParam") {
        cosmo::MsgInfoRecv req;
        req.devId = "WRONG-DEV-ID";
        std::error_condition errc;

        auto result = appInfoSvc.GetSystemOverviewInfo(req, errc);
        REQUIRE(errc == cosmo::util::ErrorEnum::InvalidParam);
    }
}

TEST_CASE("AppInfoServiceImpl: Thread safety of GetNumber", "[appinfo][service][thread]") {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::AppInfoServiceImpl appInfoSvc;

    constexpr int kThreadCount = 4;
    constexpr int kIterations  = 100;
    std::vector<std::thread> threads;
    std::vector<size_t> collected(kThreadCount * kIterations);
    std::atomic<size_t> idx{0};

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kIterations; ++i) {
                size_t pos     = idx.fetch_add(1);
                collected[pos] = appInfoSvc.GetNumber();
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // All values should be unique (atomic increment)
    std::sort(collected.begin(), collected.end());
    for (size_t i = 1; i < collected.size(); ++i) {
        REQUIRE(collected[i] == collected[i - 1] + 1);
    }
}
