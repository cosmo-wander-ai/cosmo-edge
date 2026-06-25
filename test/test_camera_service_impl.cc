#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
/*
 * test_camera_service_impl.cc - CameraServiceImpl 核心路径单元测试
 *
 * 测试策略: 不调用 InitCameraEntities（需要文件系统和DI），
 * 仅测试不依赖外部状态的纯逻辑路径。
 */
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "mock/MockServiceRegistry.h"
#include "service/camera/impl/CameraServiceImpl.h"

using namespace cosmo::service;
using namespace cosmo;

// ============================================================
// 构造 / 析构安全
// ============================================================

TEST_CASE("CameraServiceImpl: construction and destruction without Init", "[CameraService]") {
    // 仅构造和析构，不调用 InitCameraEntities
    REQUIRE_NOTHROW([]() {
        CameraServiceImpl svc;
        // destructor runs here
    }());
}

// ============================================================
// 查询参数校验
// ============================================================

TEST_CASE("CameraServiceImpl: Query with invalid pagination returns empty", "[CameraService]") {
    CameraServiceImpl svc;
    size_t total = 0;

    SECTION("pageNum = 0") {
        auto result = svc.Query("", 0, 0, 10, total);
        REQUIRE(result.empty());
    }

    SECTION("pageSize = 0") {
        auto result = svc.Query("", 0, 1, 0, total);
        REQUIRE(result.empty());
    }

    SECTION("negative page") {
        auto result = svc.Query("", 0, -1, 10, total);
        REQUIRE(result.empty());
    }
}

TEST_CASE("CameraServiceImpl: Query with valid pagination but no cameras returns empty", "[CameraService]") {
    CameraServiceImpl svc;
    size_t total = 0;
    auto result  = svc.Query("", 0, 1, 10, total);
    REQUIRE(result.empty());
    REQUIRE(total == 0);
}

// ============================================================
// 非存在 Camera 的操作安全
// ============================================================

TEST_CASE("CameraServiceImpl: operations on non-existent camera return proper errors", "[CameraService]") {
    CameraServiceImpl svc;

    SECTION("Delete non-existent camera returns CameraNotExist") {
        auto ret = svc.Delete("cam_not_exist");
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("Update non-existent camera returns CameraNotExist") {
        cosmo::MsgCameraInfo config;
        config.videoChannelId = "cam_not_exist";
        auto ret              = svc.Update(config);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("GetTasks for non-existent camera returns empty") {
        auto tasks = svc.GetTasks("cam_not_exist");
        REQUIRE(tasks.empty());
    }

    SECTION("CaptureImage for non-existent camera returns nullptr") {
        auto frame = svc.CaptureImage("cam_not_exist");
        REQUIRE(frame == nullptr);
    }

    SECTION("GetChannelName for non-existent camera returns empty") {
        auto name = svc.GetChannelName("cam_not_exist");
        REQUIRE(name.empty());
    }

    SECTION("QuerySwitch for non-existent camera returns CameraNotExist") {
        bool enable = false;
        auto ret    = svc.QuerySwitch("cam_not_exist", "alg1", enable);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("ModifyTaskParam for non-existent camera returns CameraNotExist") {
        cosmo::MsgTaskConfig params;
        auto ret = svc.ModifyTaskParam("cam_not_exist", "alg1", params);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("QueryTaskParam for non-existent camera returns CameraNotExist") {
        std::vector<cosmo::MsgDynamicKeyValue> params;
        auto ret = svc.QueryTaskParam("cam_not_exist", "alg1", params);
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }

    SECTION("DeleteTask for non-existent camera returns CameraNotExist") {
        auto ret = svc.DeleteTask("cam_not_exist", "alg1");
        REQUIRE(ret == cosmo::util::ErrorEnum::CameraNotExist);
    }
}

// ============================================================
// Notify 通知空 ID 列表安全
// ============================================================

TEST_CASE("CameraServiceImpl: NotifyAlgorithms with empty list does not crash", "[CameraService]") {
    CameraServiceImpl svc;

    SECTION("NotifyAlgorithmsChanged with empty ids") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({}, true));
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({}, false));
    }

    SECTION("NotifyAlgorithmsDeleted with empty ids") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsDeleted({}));
    }

    SECTION("NotifyAlgorithmsChanged with non-existent ids does not crash") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({"non_existent_1", "non_existent_2"}, false));
    }

    SECTION("NotifyAlgorithmsDeleted with non-existent ids does not crash") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsDeleted({"non_existent_1"}));
    }
}

// ============================================================
// ScheduleInUse 无 Camera 时
// ============================================================

TEST_CASE("CameraServiceImpl: ScheduleInUse returns false when no cameras", "[CameraService]") {
    CameraServiceImpl svc;
    REQUIRE_FALSE(svc.ScheduleInUse("sched_not_exist"));
}

// ============================================================
// 并发查询安全
// ============================================================

TEST_CASE("CameraServiceImpl: concurrent Query calls are safe", "[CameraService][concurrency]") {
    CameraServiceImpl svc;

    std::atomic<bool> stop{false};
    std::atomic<int> readCount{0};

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                size_t total = 0;
                auto result  = svc.Query("", 0, 1, 10, total);
                (void)result;
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);

    for (auto& t : readers) {
        t.join();
    }

    REQUIRE(readCount.load() > 0);
}

// ============================================================
// 生命周期完整测试 (依赖 DI 注入)
// ============================================================

TEST_CASE("CameraServiceImpl: Full lifecycle (Add, Query, Update, Delete)", "[CameraService]") {
    std::string testBaseDir = "/tmp/cosmo_camera_test_" +
                              std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);

    cosmo::test::MockServiceRegistry mocks;
    cosmo::path::OverrideRootPathForTest(testBaseDir, testBaseDir);

    // 我们必须手动初始化 Entities 以防由于文件系统变化导致的崩溃
    CameraServiceImpl svc;
    svc.InitCameraEntities();

    std::string newCameraId;

    SECTION("Add Camera successfully") {
        cosmo::MsgCameraInfo config;
        config.channelName = "Test_Camera_1";
        config.url         = "rtsp://test/1";

        auto ret = svc.Add(config, newCameraId);
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);
        REQUIRE(!newCameraId.empty());
        REQUIRE(newCameraId.find("RT") == 0);

        // Verify Query
        size_t total = 0;
        auto list    = svc.Query("Test_Camera", -1, 1, 10, total);
        REQUIRE(total == 1);
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].channelName == "Test_Camera_1");

        // Verify Update
        config.videoChannelId = newCameraId;
        config.channelName    = "Test_Camera_Updated";
        ret                   = svc.Update(config);
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);

        list = svc.Query("", -1, 1, 10, total);
        REQUIRE(list.size() == 1);
        REQUIRE(list[0].channelName == "Test_Camera_Updated");

        // Verify GetChannelName
        auto name = svc.GetChannelName(newCameraId);
        REQUIRE(name == "Test_Camera_Updated");

        // Verify Delete
        ret = svc.Delete(newCameraId);
        REQUIRE(ret == cosmo::util::ErrorEnum::Success);

        list = svc.Query("", -1, 1, 10, total);
        REQUIRE(total == 0);
        REQUIRE(list.empty());
    }

    std::filesystem::remove_all(testBaseDir);
}

// ============================================================
// QueryUsbCameraList DI 测试
// ============================================================

TEST_CASE("CameraServiceImpl: QueryUsbCameraList via Dependency Injection", "[CameraService]") {
    std::string testDevDir =
        "/tmp/cosmo_dev_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testDevDir);

    // Create fake video device files
    std::ofstream(testDevDir + "/video0");
    std::ofstream(testDevDir + "/video1");
    std::ofstream(testDevDir + "/video99");
    std::ofstream(testDevDir + "/notavideo");

    CameraServiceImpl svc;
    svc.SetUsbDeviceDirMock(testDevDir);

    // 注入一个简单的 Check Mock，所有 video0 都失败，其他的都成功
    svc.SetUsbDeviceCheckMock([](const std::string& path) {
        if (path.find("video0") != std::string::npos)
            return false;
        return true;
    });

    auto devices = svc.QueryUsbCameraList();

    // Should find video1 and video99, but skip video0 (due to check) and notavideo (regex)
    REQUIRE(devices.size() == 2);

    // Check elements
    bool hasVideo1 = false, hasVideo99 = false;
    for (const auto& dev : devices) {
        if (dev.usbDeviceIndex == 1)
            hasVideo1 = true;
        if (dev.usbDeviceIndex == 99)
            hasVideo99 = true;
    }
    REQUIRE(hasVideo1);
    REQUIRE(hasVideo99);

    std::filesystem::remove_all(testDevDir);
}
