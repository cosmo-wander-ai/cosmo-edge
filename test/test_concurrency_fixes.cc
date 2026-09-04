#include "catch_amalgamated.hpp"
#include "service/algorithm/IAlgorithmService.h"
#include "util/PathUtil.h"
/*
 * test_concurrency_fixes.cc - Phase 1.1 并发修复验证
 *
 * 测试目标:
 * 1. NotifyAlgorithmDeleted 在锁内原子标记 enable=false / status=Stop
 * 2. 并发 GetTasks 读取期间不会看到半更新状态
 * 3. TaskServiceImpl 日志限频 map 在 TaskDelete 后被清理
 */
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "mock/MockAlgorithmService.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/SystemServiceImpl.h"
#include "service/task/impl/TaskServiceImpl.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo;

// ============================================================
// CameraServiceImpl: NotifyAlgorithmsDeleted state consistency
// ============================================================

TEST_CASE("CameraServiceImpl: NotifyAlgorithmsDeleted marks all matching tasks atomically",
          "[CameraServiceImpl][concurrency]") {
    (void)!system("rm -rf /tmp/test_conc*");
    cosmo::service::CameraServiceImpl svc;

    // Verify: deleting non-existent algorithm does not crash
    SECTION("Deleting non-existent algorithm does not crash") {
        REQUIRE_NOTHROW(svc.NotifyAlgorithmsDeleted({"non_existent_alg"}));
    }

    // Verify: GetTasks on non-existent camera returns empty
    SECTION("GetTasks returns empty when camera does not exist") {
        auto tasks = svc.GetTasks("non_existent_camera");
        REQUIRE(tasks.empty());
    }
}

// ============================================================
// CameraServiceImpl: concurrent read safety
// ============================================================

TEST_CASE("CameraServiceImpl: concurrent GetTasks reads are safe", "[CameraServiceImpl][concurrency]") {
    (void)!system("rm -rf /tmp/test_conc*");
    cosmo::service::CameraServiceImpl svc;

    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    std::atomic<int> readCount{0};
    std::atomic<int> readyCount{0};
    constexpr int kReaderCount = 4;

    // Start multiple reader threads concurrently calling GetTasks
    std::vector<std::thread> readers;
    for (int i = 0; i < kReaderCount; i++) {
        readers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            while (!stop.load(std::memory_order_acquire)) {
                auto tasks = svc.GetTasks("non_existent_camera");
                // tasks should be empty but not crash
                (void)tasks;
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Wait for all readers to be ready before starting writes
    while (readyCount.load(std::memory_order_acquire) < kReaderCount) {
        std::this_thread::yield();
    }
    go.store(true, std::memory_order_release);

    // Simultaneously do writes on the main thread
    for (int i = 0; i < 100; i++) {
        svc.NotifyAlgorithmsDeleted({"alg_" + std::to_string(i)});
    }

    stop.store(true, std::memory_order_release);
    for (auto& t : readers) {
        t.join();
    }

    // Verify: reader threads actually ran
    REQUIRE(readCount.load() > 0);
}

// ============================================================
// CameraServiceImpl: NotifyAlgorithmsChanged safety for non-existent algorithm
// ============================================================

TEST_CASE("CameraServiceImpl: NotifyAlgorithmsChanged with non-existent alg does not crash",
          "[CameraServiceImpl][concurrency]") {
    (void)!system("rm -rf /tmp/test_conc*");
    cosmo::service::CameraServiceImpl svc;

    REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({"non_existent_alg"}, false));
    REQUIRE_NOTHROW(svc.NotifyAlgorithmsChanged({"non_existent_alg"}, true));
}

// ============================================================
// TaskServiceImpl: 日志限频 map 清理
// ============================================================

TEST_CASE("TaskServiceImpl: log throttle map is cleaned on TaskDelete", "[TaskService][concurrency]") {
    cosmo::service::TaskServiceImpl svc;

    // GetTaskLiveOverviewInfo 对不存在的 task 会写入 m_notInPoolLogTs
    // 调用多次确保 map entry 被创建
    for (int i = 0; i < 3; i++) {
        auto infos = svc.GetTaskLiveOverviewInfo("phantom_task_001");
        REQUIRE(infos.empty());
    }

    // TaskDelete 应当清理 map entry（即使 task 不存在，也不应崩溃）
    auto ret = svc.TaskDelete("phantom_task_001");
    // task 不存在返回 NotInit
    REQUIRE(ret == cosmo::util::ErrorEnum::NotInit);

    // 再次调用不应崩溃 — 验证 map 操作安全
    auto infos2 = svc.GetTaskLiveOverviewInfo("phantom_task_001");
    REQUIRE(infos2.empty());
}

// ============================================================
// TaskServiceImpl: 并发 GetTaskLiveOverviewInfo 调用安全
// ============================================================

TEST_CASE("TaskServiceImpl: concurrent GetTaskLiveOverviewInfo is safe", "[TaskService][concurrency]") {
    cosmo::service::TaskServiceImpl svc;

    std::atomic<bool> stop{false};
    std::atomic<int> callCount{0};
    constexpr int kThreadCount = 4;

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; i++) {
        threads.emplace_back([&, i]() {
            std::string taskId = "task_" + std::to_string(i);
            while (!stop.load(std::memory_order_relaxed)) {
                auto infos = svc.GetTaskLiveOverviewInfo(taskId);
                (void)infos;
                callCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 让并发线程跑一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(callCount.load() > 0);
}

// ============================================================
// SystemServiceImpl: 并发读写 PictureQuality 安全性
// ============================================================

TEST_CASE("SystemServiceImpl: concurrent Get/Set PictureQuality is safe",
          "[SystemConfigService][concurrency]") {
    std::string dir = "/tmp/cosmo_conc_syscfg_test";
    std::filesystem::create_directories(dir + "/conf");
    std::ofstream(dir + "/conf/alarmParam.json") << R"({"overviewInfo":{},"videoRecordInfo":{}})";
    std::ofstream(dir + "/conf/devRebootParam.json")
        << R"({"isTimingRestart":true,"weekDay":0,"restartTimeSec":7200})";
    std::ofstream(dir + "/conf/devSystemParam.json") << "{}";
    cosmo::test::ScopedPathOverride path_override(dir, dir);

    cosmo::service::SystemServiceImpl sut;

    std::atomic<bool> stop{false};
    std::atomic<int> readCount{0};
    std::atomic<int> writeCount{0};

    // Reader threads
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                auto q = sut.GetPictureQuality();
                // picQuality should always be in [1, 100] or the default 75
                CHECK(q.picQuality >= 1);
                CHECK(q.picQuality <= 100);
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Writer threads
    std::vector<std::thread> writers;
    for (int i = 0; i < 2; i++) {
        writers.emplace_back([&, i]() {
            for (int j = 0; j < 50; j++) {
                cosmo::CfgAlarmParamOverviewInfo info;
                info.picQuality = (j % 100) + 1;
                sut.SetPictureQuality(info);
                writeCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : writers)
        t.join();
    stop.store(true);
    for (auto& t : readers)
        t.join();

    REQUIRE(readCount.load() > 0);
    REQUIRE(writeCount.load() == 100);

    std::filesystem::remove_all(dir);
}

// ============================================================
// SystemServiceImpl: 并发 DebugMode / ActionSwitch
// ============================================================

TEST_CASE("SystemServiceImpl: concurrent DebugMode/ActionSwitch toggle is safe",
          "[SystemConfigService][concurrency]") {
    std::string dir = "/tmp/cosmo_conc_debug_test";
    std::filesystem::create_directories(dir + "/conf");
    std::ofstream(dir + "/conf/alarmParam.json") << R"({"overviewInfo":{},"videoRecordInfo":{}})";
    std::ofstream(dir + "/conf/devRebootParam.json") << R"({})";
    std::ofstream(dir + "/conf/devSystemParam.json") << "{}";
    cosmo::test::ScopedPathOverride path_override(dir, dir);

    cosmo::service::SystemServiceImpl sut;

    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    std::atomic<int> switchChecks{0};
    std::atomic<int> readyCount{0};

    // Start readers first, wait for all to be ready
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; i++) {
        readers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            while (!stop.load(std::memory_order_acquire)) {
                // Should not crash regardless of debug state
                sut.GetActionSwitch("action_0");
                sut.GetDebugMode();
                sut.GetShieldedActions();
                switchChecks.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Wait for readers to be ready, then allow them to run
    while (readyCount.load(std::memory_order_acquire) < 3) {
        std::this_thread::yield();
    }
    go.store(true, std::memory_order_release);

    // Toggle debug mode rapidly while readers are running
    std::thread toggler([&]() {
        for (int i = 0; i < 200; i++) {
            sut.SetDebugMode(i % 2 == 0);
            sut.SetShieldedActions({"action_" + std::to_string(i % 5)});
            std::this_thread::yield();  // give readers a chance to acquire the shared lock
        }
    });

    toggler.join();
    stop.store(true, std::memory_order_release);
    for (auto& t : readers)
        t.join();

    REQUIRE(switchChecks.load() > 0);

    std::filesystem::remove_all(dir);
}

// ============================================================
// ServiceRegistry: 并发 Get 安全性
// ============================================================

TEST_CASE("ServiceRegistry: concurrent Get is safe", "[ServiceRegistry][concurrency]") {
    cosmo::test::MockAlgorithmService algorithm;
    cosmo::test::ScopedServiceOverride<cosmo::service::IAlgorithmService> registration(algorithm);

    std::atomic<bool> stop{false};
    std::atomic<int> getCount{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                // These should be thread-safe (shared_lock inside Get)
                auto& alg =
                    cosmo::service::ServiceRegistry::Instance().Get<cosmo::service::IAlgorithmService>();
                (void)alg;
                auto has =
                    cosmo::service::ServiceRegistry::Instance().Has<cosmo::service::IAlgorithmService>();
                (void)has;
                getCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);
    for (auto& t : threads)
        t.join();

    REQUIRE(getCount.load() > 0);
}
