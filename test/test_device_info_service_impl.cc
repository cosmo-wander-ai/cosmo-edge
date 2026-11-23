#include "catch_amalgamated.hpp"
/*
 * test_device_info_service_impl.cc — DeviceInfoServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: DeviceInfoServiceImpl reads hardware info at construction via
 * platform-specific APIs (bm_get_*, /proc/*). Full functional tests require
 * aarch64 device. We tag device-dependent tests with [.device].
 * Cross-platform tests focus on construction safety and basic getters.
 */
#include <algorithm>
#include <sstream>
#include <thread>

#include "mock/MockAppInfoService.h"
#include "mock/MockTaskService.h"
#include "service/system/impl/DeviceInfoServiceImpl.h"
#include "service/system/impl/HardwareQueryUtil.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;
using trompeloeil::_;

TEST_CASE("AX memory accounting verifies separate boot allocations", "[DeviceInfoService][ax-memory]") {
    constexpr int64_t kMiB          = 1024 * 1024;
    constexpr int64_t kLinuxVisible = 6041500LL * 1024;
    const std::string partition =
        "+---PARTITION: Phys(0x27E200000, 0x4FE1FFFFF), Size=10485760KB(10240MB), NAME=\"anonymous\"\n";
    const std::string summary =
        "total size=10485760KB(10240MB),used=283328KB(276MB + 704KB),remain=10202432KB(9963MB + 320KB),"
        "partition_number=1,block_number=42\n";
    std::istringstream boot("console=ttyS0 mem=6114M quiet");
    std::istringstream iomem(
        "100000000-133ffffff : System RAM\n"
        "  108000000-1087fffff : Kernel code\n"
        "136401000-13dffffff : System RAM\n"
        "140000000-27e1fffff : System RAM\n");
    std::istringstream cmm(partition + summary);

    SECTION("Complete boot allocation") {
        const auto usage = detail::HardwareQueryUtil::ParseAxMemoryUsage(cmm, boot, iomem, kLinuxVisible);
        CHECK(usage.cmmTotalBytes == 10240 * kMiB);
        CHECK(usage.cmmAvailableBytes == 10202432LL * 1024);
        CHECK(usage.linuxAllocatedBytes == 6114 * kMiB);
        CHECK(usage.bootAllocatedBytes == 16354 * kMiB);
    }
    SECTION("Masked addresses keep the separately reported counters") {
        iomem.str("00000000-00000000 : System RAM\n");
        const auto usage = detail::HardwareQueryUtil::ParseAxMemoryUsage(cmm, boot, iomem, kLinuxVisible);
        CHECK(usage.cmmTotalBytes == 10240 * kMiB);
        CHECK(usage.linuxAllocatedBytes == 6114 * kMiB);
        CHECK(usage.bootAllocatedBytes == 0);
    }
    SECTION("An overlapping partition cannot contribute to the combined total") {
        cmm.str(
            "+---PARTITION: Phys(0x27E100000, 0x4FE0FFFFF), Size=10485760KB(10240MB), NAME=\"anonymous\"\n" +
            summary);
        const auto usage = detail::HardwareQueryUtil::ParseAxMemoryUsage(cmm, boot, iomem, kLinuxVisible);
        CHECK(usage.cmmTotalBytes == 10240 * kMiB);
        CHECK(usage.bootAllocatedBytes == 0);
    }
    SECTION("An inconsistent driver summary is unavailable") {
        cmm.str(partition +
                "total size=10485760KB,used=283328KB,remain=10202433KB,partition_number=1,block_number=42\n");
        const auto usage = detail::HardwareQueryUtil::ParseAxMemoryUsage(cmm, boot, iomem, kLinuxVisible);
        CHECK(usage.cmmTotalBytes == 0);
        CHECK(usage.cmmAvailableBytes == -1);
        CHECK(usage.bootAllocatedBytes == 0);
    }
}

TEST_CASE("DeviceInfoServiceImpl: GetDeviceInfo returns populated struct", "[DeviceInfoService][.device]") {
    cosmo::test::MockAppInfoService appInfoSvc;
    cosmo::test::ScopedServiceOverride<IAppInfoService> registration(appInfoSvc);
    ALLOW_CALL(appInfoSvc, GetAppRuntime()).RETURN(0);
    DeviceInfoServiceImpl sut;

    auto info = sut.GetDeviceInfo();
    // Software version should always be available
    REQUIRE(!info.softwareVersion.empty());
}

TEST_CASE("DeviceInfoServiceImpl: GetDevSn and GetDevModel", "[DeviceInfoService][.device]") {
    DeviceInfoServiceImpl sut;

    SECTION("GetDevModel returns non-empty") {
        auto model = sut.GetDevModel();
        REQUIRE(!model.empty());
    }
}

TEST_CASE("DeviceInfoServiceImpl: GetCpuUtilization returns valid range", "[DeviceInfoService][.device]") {
    DeviceInfoServiceImpl sut;

    // Give monitor thread time to poll
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto cpu = sut.GetCpuUtilization();
    REQUIRE(cpu >= 0.0);
    REQUIRE(cpu <= 1.0);
}

TEST_CASE("DeviceInfoServiceImpl: GetGpuNum returns at least 1", "[DeviceInfoService][.device]") {
    DeviceInfoServiceImpl sut;

    REQUIRE(sut.GetGpuNum() >= 1);
}

#ifdef COSMO_NN_USE_RKNN_BACKEND
TEST_CASE("DeviceInfoServiceImpl: RKNN shared memory is exposed once", "[DeviceInfoService][.device][rknn]") {
    cosmo::test::MockTaskService taskSvc;
    cosmo::test::ScopedServiceOverride<ITaskQuery> registration(taskSvc);
    ALLOW_CALL(taskSvc, PacketStatus(_, _, _, _)).LR_SIDE_EFFECT(_1 = 0; _2 = 0; _3 = 0; _4 = 0);
    DeviceInfoServiceImpl sut;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    double custom_score = 0.0;
    const auto items    = sut.GetHardwareResource(custom_score);
    const auto general  = std::find_if(
        items.begin(), items.end(), [](const auto& item) { return item.key == "generalMemoryUtilization"; });
    REQUIRE(general != items.end());
    CHECK(general->memoryDomain == "system");
    CHECK((general->usedSize.find("GiB") != std::string::npos ||
           general->usedSize.find("MiB") != std::string::npos));

    const auto accelerator_memory_count = std::count_if(items.begin(), items.end(), [](const auto& item) {
        return item.key == "specialMemoryUtilization" || item.key == "modelMemoryUtilization" ||
               item.key == "pictureMemoryUtilization" || item.key == "TPPMemoryUtilization";
    });
    CHECK(accelerator_memory_count == 0);
    CHECK(sut.GetGpuUtilization().memoryDomain == "shared-system");
}
#endif
