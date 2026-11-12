#include "catch_amalgamated.hpp"
// Unit tests for AppInfoServiceImpl — validates state management,
// overview queries through IHardwareQuery, and thread-safe property accessors.

#include <thread>

#include "mock/MockDeviceInfoService.h"
#include "mock/MockTaskService.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/AppInfoServiceImpl.h"
#include "support/ScopedServiceOverride.h"

TEST_CASE("AppInfoServiceImpl: State management", "[appinfo][service]") {
    cosmo::service::AppInfoServiceImpl appInfoSvc;

    SECTION("GetNumber returns incrementing values") {
        auto n1 = appInfoSvc.GetNumber();
        auto n2 = appInfoSvc.GetNumber();
        auto n3 = appInfoSvc.GetNumber();
        REQUIRE(n2 == n1 + 1);
        REQUIRE(n3 == n2 + 1);
    }
}

TEST_CASE("AppInfoServiceImpl: overview uses only narrow hardware queries",
          "[appinfo][service][hardware-query]") {
    cosmo::test::MockDeviceInfoService hardware;
    cosmo::test::MockTaskService tasks;
    cosmo::test::ScopedServiceOverride<cosmo::service::IHardwareQuery> hardware_registration(hardware);
    cosmo::test::ScopedServiceOverride<cosmo::service::ITaskQuery> task_registration(tasks);
    REQUIRE_FALSE(cosmo::service::ServiceRegistry::Instance().Has<cosmo::service::IDeviceInfoService>());

    cosmo::service::AppInfoServiceImpl app;
    app.SetDevId("overview-test");
    cosmo::MsgGpuInfo gpu;
    gpu.gpuusage        = 0.5;
    gpu.gpumemtotal     = 4096;
    gpu.gpumemavailable = 1024;
    gpu.gpuCapacity     = "test-capacity";
    gpu.gpudevusage.resize(2);
    gpu.gpudevusage[0].gpumemtotal     = 3072;
    gpu.gpudevusage[0].gpumemavailable = 768;
    gpu.gpudevusage[1].gpumemtotal     = 1024;
    gpu.gpudevusage[1].gpumemavailable = 256;
    cosmo::MsgMemoryInfo memory;
    memory.memtotal     = 16384;
    memory.memavailable = 8192;
    cosmo::MsgDiskInfo disk;
    disk.disktotal     = 32768;
    disk.diskavailable = 16384;
    cosmo::MsgNetInfo network;
    network.networkupperrate    = 12;
    network.networkdownwardrate = 34;

    trompeloeil::sequence sampling;
    REQUIRE_CALL(hardware, GetCpuUtilization()).IN_SEQUENCE(sampling).RETURN(0.25);
    REQUIRE_CALL(hardware, GetGpuUtilization()).IN_SEQUENCE(sampling).RETURN(gpu);
    REQUIRE_CALL(hardware, GetMemoryUtilization()).IN_SEQUENCE(sampling).RETURN(memory);
    REQUIRE_CALL(hardware, GetDiskUtilization()).IN_SEQUENCE(sampling).RETURN(disk);
    REQUIRE_CALL(hardware, GetNetUtilization()).IN_SEQUENCE(sampling).RETURN(network);
    REQUIRE_CALL(tasks, QueueStatusDto(trompeloeil::_, 30)).IN_SEQUENCE(sampling);

    cosmo::MsgInfoRecv request;
    request.devId              = "overview-test";
    std::error_condition error = cosmo::util::ErrorEnum::Success;
    const auto result          = app.GetSystemOverviewInfo(request, error);
    REQUIRE(error == cosmo::util::ErrorEnum::Success);
    CHECK(result.devId == request.devId);
    CHECK(result.cpuUsage == Catch::Approx(0.25));
    CHECK(result.gpuUsage == Catch::Approx(0.5));
    CHECK(result.memTotal == 16384);
    CHECK(result.memAvailable == 8192);
    CHECK(result.gpuMemTotal == 4096);
    CHECK(result.gpuMemAvailable == 1024);
    CHECK(result.gpuCapacity == "test-capacity");
    CHECK(result.gpuModelMemTotal == 3072);
    CHECK(result.gpuModelMemAvailable == 768);
    CHECK(result.gpuPicMemTotal == 1024);
    CHECK(result.gpuPicMemAvailable == 256);
    REQUIRE(result.gpuMemDetails.size() == 2);
    CHECK(result.diskTotal == 32768);
    CHECK(result.diskAvailable == 16384);
    CHECK(result.networkUpperrate == 12);
    CHECK(result.networkDownwardrate == 34);
}

TEST_CASE("AppInfoServiceImpl: GetPagedLogs validation", "[appinfo][service]") {
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

    SECTION("Excessive pageSize returns error") {
        cosmo::MsgQueryLogsRecv req;
        req.pageNum  = 1;
        req.pageSize = 1001;
        std::error_condition errc;

        auto result = appInfoSvc.GetPagedLogs(req, errc);
        REQUIRE(errc == cosmo::util::ErrorEnum::ParameterException);
    }
}

TEST_CASE("AppInfoServiceImpl: GetSystemOverviewInfo device validation", "[appinfo][service]") {
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
