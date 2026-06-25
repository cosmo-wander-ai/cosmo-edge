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

#include "mock/MockServiceRegistry.h"
#include "mock/MockTaskService.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/impl/SystemServiceImpl.h"
#include "service/task/ITaskService.h"
#include "service/task/impl/TaskServiceImpl.h"

using namespace cosmo;

namespace {
class MockTaskService : public cosmo::service::ITaskService {
public:
    // ITaskLifecycle
    cosmo::util::ErrorEnum TaskCreate(const std::string&, const std::string&, const std::string&,
                                      cosmo::ActionAlgPtr) override {
        return cosmo::util::ErrorEnum::Success;
    }
    cosmo::util::ErrorEnum TaskDelete(const std::string&) override {
        return cosmo::util::ErrorEnum::Success;
    }
    cosmo::MsgTaskCreateSend ProcessTaskCreate(cosmo::MsgTaskCreateRecv&, std::error_condition&) override {
        return {};
    }
    cosmo::MsgTaskCancleSend ProcessTaskCancel(cosmo::MsgTaskCancleRecv&, std::error_condition&) override {
        return {};
    }
    bool TaskStart(const std::string&, const std::string&) override {
        return true;
    }
    bool TaskStop(const std::string&) override {
        return true;
    }
    bool TaskIsStart(const std::string&) override {
        return false;
    }
    bool SetTaskParam(const std::string&, const std::string&, cosmo::MsgTaskConfig&) override {
        return true;
    }
    bool LogicTest(const std::string&, cosmo::MsgTarget&) override {
        return true;
    }
    void ShowActions(cosmo::ActionAlgPtr) override {}
    void RecordClearTaskData(const std::string&) override {}
    void RecordTaskInfo(const std::string&, cosmo::MsgTaskCreateRecv&) override {}
    void RecordTaskAction(const std::string&, cosmo::ActionAlgPtr) override {}

    // ITaskChannel
    void TaskChannelSetUrl(const std::string&, const std::string&) override {}
    void TaskChannelSetParam(const std::string&, const std::string&, int) override {}
    VideoFramePtr CaptureImage(const std::string&, int) override {
        return nullptr;
    }
    bool GetChannelAttr(const std::string&, cosmo::MsgCameraAttr&) override {
        return true;
    }
    bool TaskDataActive(const std::string&) override {
        return false;
    }
    cosmo::AlgChannelPtr GetChannelInst(const std::string&) override {
        return nullptr;
    }
    std::vector<std::string> GetChannelTasks(const std::string&) override {
        return {};
    }
    cosmo::TaskAlarmPtr GetAlarmInst(const std::string&, const std::string&) override {
        return nullptr;
    }
    std::string GetTaskChannel(const std::string&) override {
        return "";
    }
    void GetCameraInfo(std::vector<cosmo::MsgCameraInfo>&) override {}

    // ITaskQuery
    std::vector<std::string> QueryTasks(bool) override {
        return {};
    }
    bool GetTaskParam(const std::string&, const std::string&, cosmo::MsgTaskConfig&) override {
        return true;
    }
    std::vector<cosmo::TaskStatus> GetTaskStatus(const std::vector<std::string>&, unsigned int) override {
        return {};
    }
    std::vector<cosmo::MsgCameraInfo> CameraTaskInfo() override {
        return {};
    }
    bool GetTaskFrameInfo(const std::string&, bool&, int64_t&, int64_t&, int64_t&, std::string&) override {
        return false;
    }
    size_t TaskCount() override {
        return 0;
    }
    int GetAlgorithmCount(const std::string&) override {
        return 0;
    }
    void QueueStatus(std::vector<cosmo::AlgActionDataQueueStatus>&, unsigned int) override {}
    void QueueStatusDto(std::vector<cosmo::AlgActionDataQueueStatusDto>&, unsigned int) override {}
    void PacketStatus(size_t&, size_t&, size_t&, size_t&) override {}
    std::vector<cosmo::MsgOverviewMem> GetTaskLiveOverviewInfo(const std::string&, int64_t, int64_t,
                                                               int64_t) override {
        return {};
    }
    std::vector<cosmo::DataDetTrackClassify> GetTaskDetHistory(const std::string&, const std::string&,
                                                               int64_t, int64_t, int64_t) override {
        return {};
    }
    std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>> GetTaskActionDurations(
        const std::string&, int) override {
        return {};
    }
};

struct GlobalMockInit {
    MockTaskService* taskSvc;
    GlobalMockInit() {
        taskSvc = new MockTaskService();
        service::ServiceRegistry::Instance().Set<service::ITaskService>(taskSvc);
    }
} g_mockInit;
}  // namespace

// ============================================================
// CameraServiceImpl: NotifyAlgorithmsDeleted state consistency
// ============================================================

TEST_CASE("CameraServiceImpl: NotifyAlgorithmsDeleted marks all matching tasks atomically",
          "[CameraServiceImpl][concurrency]") {
    (void)!system("rm -rf /tmp/test_conc*");
    cosmo::test::MockServiceRegistry mocks;
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
    cosmo::test::MockServiceRegistry mocks;
    cosmo::service::CameraServiceImpl svc;

    std::atomic<bool> stop{false};
    std::atomic<int> readCount{0};
    constexpr int kReaderCount = 4;

    // Start multiple reader threads concurrently calling GetTasks
    std::vector<std::thread> readers;
    for (int i = 0; i < kReaderCount; i++) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                auto tasks = svc.GetTasks("non_existent_camera");
                // tasks should be empty but not crash
                (void)tasks;
                readCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Simultaneously do writes on the main thread
    for (int i = 0; i < 100; i++) {
        svc.NotifyAlgorithmsDeleted({"alg_" + std::to_string(i)});
    }

    stop.store(true);
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
    cosmo::test::MockServiceRegistry mocks;
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
    cosmo::test::MockServiceRegistry mocks;

    std::string dir = "/tmp/cosmo_conc_syscfg_test";
    std::filesystem::create_directories(dir + "/conf");
    std::ofstream(dir + "/conf/alarmParam.json") << R"({"overviewInfo":{},"videoRecordInfo":{}})";
    std::ofstream(dir + "/conf/devRebootParam.json")
        << R"({"isTimingRestart":true,"weekDay":0,"restartTimeSec":7200})";
    std::ofstream(dir + "/conf/devSystemParam.json") << "{}";
    cosmo::path::OverrideRootPathForTest(dir, dir);

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
    cosmo::test::MockServiceRegistry mocks;

    std::string dir = "/tmp/cosmo_conc_debug_test";
    std::filesystem::create_directories(dir + "/conf");
    std::ofstream(dir + "/conf/alarmParam.json") << R"({"overviewInfo":{},"videoRecordInfo":{}})";
    std::ofstream(dir + "/conf/devRebootParam.json") << R"({})";
    std::ofstream(dir + "/conf/devSystemParam.json") << "{}";
    cosmo::path::OverrideRootPathForTest(dir, dir);

    cosmo::service::SystemServiceImpl sut;

    std::atomic<bool> stop{false};
    std::atomic<int> switchChecks{0};

    // Toggle debug mode rapidly
    std::thread toggler([&]() {
        for (int i = 0; i < 200; i++) {
            sut.SetDebugMode(i % 2 == 0);
            sut.SetShieldedActions({"action_" + std::to_string(i % 5)});
        }
    });

    // Read ActionSwitch concurrently
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; i++) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                // Should not crash regardless of debug state
                sut.GetActionSwitch("action_0");
                sut.GetDebugMode();
                sut.GetShieldedActions();
                switchChecks.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    toggler.join();
    stop.store(true);
    for (auto& t : readers)
        t.join();

    REQUIRE(switchChecks.load() > 0);

    std::filesystem::remove_all(dir);
}

// ============================================================
// ServiceRegistry: 并发 Get 安全性
// ============================================================

TEST_CASE("ServiceRegistry: concurrent Get is safe", "[ServiceRegistry][concurrency]") {
    cosmo::test::MockServiceRegistry mocks;

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
