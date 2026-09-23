#include "catch_amalgamated.hpp"
#include "util/PathUtil.h"
/*
 * test_camera_service_impl.cc - CameraServiceImpl 核心路径单元测试
 *
 * 测试策略: 不调用 InitCameraEntities（需要文件系统和DI），
 * 仅测试不依赖外部状态的纯逻辑路径。
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>

#include "mock/MockTaskService.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "support/MockDefaults.h"
#include "support/ScopedPathOverride.h"
#include "support/ScopedServiceOverride.h"

using namespace cosmo::service;
using namespace cosmo;

namespace cosmo::service {

// 缩短通道停止宽限期，供预览生命周期测试使用。
struct CameraServiceTestAccess {
    static void SetChannelStopGraceMs(CameraServiceImpl& service, uint64_t grace_ms) {
        service.channel_stop_grace_ms_.store(grace_ms, std::memory_order_release);
    }

    static CameraEntityPtr GetCamera(CameraServiceImpl& service, const std::string& id) {
        return service.GetCamera(id);
    }

    static void UpdateChannelState(CameraServiceImpl& service, const CameraEntityPtr& camera) {
        service.UpdateChannelState(camera);
    }

    static bool TimerDetached(CameraServiceImpl& service) {
        std::lock_guard<std::mutex> lock(service.timer_mtx_);
        return service.stopping_.load(std::memory_order_acquire) && !service.timer_;
    }
};

}  // namespace cosmo::service

namespace {

struct CameraServiceDependencies {
    cosmo::test::MockTaskService taskSvc;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::ScopedServiceOverride<ITaskLifecycle> taskLifecycle{taskSvc};
    cosmo::test::ScopedServiceOverride<ITaskChannel> taskChannel{taskSvc};

    CameraServiceDependencies() {
        cosmo::test::AllowTaskMutationSuccess(taskSvc, expectations);
    }
};

}  // namespace

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

TEST_CASE("CameraServiceImpl: Stop is safe and idempotent without Init", "[CameraService][lifecycle]") {
    CameraServiceImpl svc;
    REQUIRE_NOTHROW(svc.Stop());
    REQUIRE_NOTHROW(svc.Stop());
    REQUIRE_NOTHROW(svc.InitCameraEntities());

    MsgCameraInfo config;
    config.videoChannelId = "camera-after-stop";
    std::string id;
    REQUIRE(svc.Add(config, id) == cosmo::util::ErrorEnum::SysErr);
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

    SECTION("Preview lease for non-existent camera returns CameraNotExist") {
        REQUIRE(svc.AcquirePreviewChannel("cam_not_exist") == cosmo::util::ErrorEnum::CameraNotExist);
        REQUIRE_NOTHROW(svc.ReleasePreviewChannel("cam_not_exist"));
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

TEST_CASE("CameraServiceImpl: live preview leases keep the channel active", "[CameraService][preview]") {
    const auto test_base = "/tmp/cosmo_camera_preview_lease_" +
                           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_base);
    cosmo::test::ScopedPathOverride path_override(test_base, test_base);

    CameraServiceDependencies mocks;
    CameraServiceImpl svc;
    CameraServiceTestAccess::SetChannelStopGraceMs(svc, 30);

    MsgCameraInfo config;
    config.videoChannelId = "preview-camera";
    config.channelName    = "Preview Camera";
    config.url            = "rtsp://127.0.0.1:1/test";
    config.channelType    = MsgCameraType::MsgCameraTypeLive;
    std::string id;
    REQUIRE(svc.Add(config, id) == cosmo::util::ErrorEnum::Success);

    trompeloeil::sequence sequence;
    REQUIRE_CALL(mocks.taskSvc, TaskIsStart("preview-camera-ChannelTask"))
        .IN_SEQUENCE(sequence)
        .RETURN(false);
    REQUIRE_CALL(mocks.taskSvc, TaskStart("preview-camera", "preview-camera-ChannelTask"))
        .IN_SEQUENCE(sequence)
        .RETURN(true);
    REQUIRE(svc.AcquirePreviewChannel("preview-camera") == cosmo::util::ErrorEnum::Success);
    REQUIRE(svc.AcquirePreviewChannel("preview-camera") == cosmo::util::ErrorEnum::Success);

    // The first release retains the shared lease. The final release starts the
    // grace period and stops the otherwise idle channel after it expires.
    svc.ReleasePreviewChannel("preview-camera");

    std::mutex stop_mutex;
    std::condition_variable stop_cv;
    bool channel_stopped = false;
    REQUIRE_CALL(mocks.taskSvc, TaskIsStart("preview-camera-ChannelTask"))
        .IN_SEQUENCE(sequence)
        .RETURN(true)
        .TIMES(2);
    REQUIRE_CALL(mocks.taskSvc, TaskStop("preview-camera-ChannelTask"))
        .IN_SEQUENCE(sequence)
        .LR_SIDE_EFFECT({
            std::lock_guard<std::mutex> lock(stop_mutex);
            channel_stopped = true;
            stop_cv.notify_all();
        })
        .RETURN(true);
    REQUIRE_CALL(mocks.taskSvc, GetChannelInst("preview-camera")).IN_SEQUENCE(sequence).RETURN(nullptr);
    svc.ReleasePreviewChannel("preview-camera");
    REQUIRE_NOTHROW(svc.ReleasePreviewChannel("preview-camera"));

    {
        std::unique_lock<std::mutex> lock(stop_mutex);
        REQUIRE(stop_cv.wait_for(lock, std::chrono::seconds(2), [&]() { return channel_stopped; }));
    }

    REQUIRE_NOTHROW(svc.Stop());
    std::filesystem::remove_all(test_base);
}

TEST_CASE("CameraServiceImpl: preview acquired during stop grace keeps channel active",
          "[CameraService][preview][concurrency]") {
    const auto test_base = "/tmp/cosmo_camera_preview_race_" +
                           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_base);
    cosmo::test::ScopedPathOverride path_override(test_base, test_base);

    CameraServiceDependencies mocks;
    CameraServiceImpl svc;
    CameraServiceTestAccess::SetChannelStopGraceMs(svc, 200);

    MsgCameraInfo config;
    config.videoChannelId = "preview-race-camera";
    config.channelName    = "Preview Race Camera";
    config.url            = "rtsp://127.0.0.1:1/test";
    config.channelType    = MsgCameraType::MsgCameraTypeLive;
    std::string id;
    REQUIRE(svc.Add(config, id) == cosmo::util::ErrorEnum::Success);

    std::atomic<bool> channel_running{true};
    ALLOW_CALL(mocks.taskSvc, TaskIsStart("preview-race-camera-ChannelTask"))
        .LR_RETURN(channel_running.load());
    ALLOW_CALL(mocks.taskSvc, TaskStart("preview-race-camera", "preview-race-camera-ChannelTask"))
        .LR_SIDE_EFFECT(channel_running.store(true))
        .RETURN(true);
    ALLOW_CALL(mocks.taskSvc, TaskStop("preview-race-camera-ChannelTask"))
        .LR_SIDE_EFFECT(channel_running.store(false))
        .RETURN(true);
    ALLOW_CALL(mocks.taskSvc, GetChannelInst("preview-race-camera")).RETURN(nullptr);

    REQUIRE(svc.AcquirePreviewChannel("preview-race-camera") == cosmo::util::ErrorEnum::Success);
    svc.ReleasePreviewChannel("preview-race-camera");
    REQUIRE(svc.AcquirePreviewChannel("preview-race-camera") == cosmo::util::ErrorEnum::Success);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    REQUIRE(channel_running.load());

    REQUIRE_NOTHROW(svc.Stop());
    std::filesystem::remove_all(test_base);
}

TEST_CASE("CameraServiceImpl: shutdown closes timer admission for an existing switch worker",
          "[CameraService][preview][concurrency][shutdown]") {
    const auto test_base = "/tmp/cosmo_camera_timer_shutdown_" +
                           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_base);
    cosmo::test::ScopedPathOverride path_override(test_base, test_base);
    CameraServiceDependencies mocks;
    CameraServiceImpl svc;

    MsgCameraInfo config;
    config.videoChannelId = "timer-shutdown-camera";
    config.url            = "rtsp://127.0.0.1:1/test";
    config.channelType    = MsgCameraType::MsgCameraTypeLive;
    std::string id;
    REQUIRE(svc.Add(config, id) == cosmo::util::ErrorEnum::Success);
    const auto camera = CameraServiceTestAccess::GetCamera(svc, id);
    REQUIRE(camera);

    const auto exercise = [&](bool pause_in_state_query) {
        std::promise<void> worker_paused;
        auto paused = worker_paused.get_future();
        std::promise<void> resume_worker;
        auto resume = resume_worker.get_future().share();
        std::atomic<int> state_queries{0};
        ALLOW_CALL(mocks.taskSvc, TaskIsStart("timer-shutdown-camera-ChannelTask"))
            .LR_SIDE_EFFECT({
                state_queries.fetch_add(1);
                if (pause_in_state_query) {
                    worker_paused.set_value();
                    resume.wait();
                }
            })
            .RETURN(true);
        REQUIRE_CALL(mocks.taskSvc, TaskStop("timer-shutdown-camera-ChannelTask")).RETURN(true);
        REQUIRE_CALL(mocks.taskSvc, TaskDelete("timer-shutdown-camera-ChannelTask"))
            .RETURN(cosmo::util::ErrorEnum::Success);

        // Exercise the actual final channel-state update of an already-admitted
        // switch worker. No model is needed to hold it at the shutdown boundary.
        camera->switch_thread_ = std::thread([&]() {
            if (!pause_in_state_query) {
                worker_paused.set_value();
                resume.wait();
            }
            CameraServiceTestAccess::UpdateChannelState(svc, camera);
        });
        const bool worker_reached_barrier =
            paused.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        bool shutdown_reached_barrier = false;
        std::thread stopper;
        if (worker_reached_barrier) {
            stopper             = std::thread([&]() { svc.Stop(); });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                shutdown_reached_barrier = CameraServiceTestAccess::TimerDetached(svc);
                if (shutdown_reached_barrier && !pause_in_state_query) {
                    // This proves Stop has drained the timer and reached the
                    // per-camera teardown/join before allowing the worker on.
                    std::lock_guard<std::mutex> lock(camera->command_mtx_);
                    shutdown_reached_barrier = camera->deleting_;
                }
                if (shutdown_reached_barrier) {
                    break;
                }
                std::this_thread::yield();
            } while (std::chrono::steady_clock::now() < deadline);
        }

        // Always release and join before assertions, including a timeout path.
        resume_worker.set_value();
        if (stopper.joinable()) {
            stopper.join();
        } else {
            camera->WaitForSwitchThread();
            svc.Stop();
        }
        REQUIRE(worker_reached_barrier);
        REQUIRE(shutdown_reached_barrier);
        CHECK(state_queries.load() == (pause_in_state_query ? 1 : 0));
        CHECK(camera->channel_stop_grace_id_ == kInvalidTaskId);
        CHECK_FALSE(camera->switch_thread_.joinable());
    };

    SECTION("worker resumes its channel update after timer destruction") {
        exercise(false);
    }
    SECTION("shutdown detaches the timer after the update has checked task state") {
        exercise(true);
    }
    std::filesystem::remove_all(test_base);
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

TEST_CASE("CameraServiceImpl: concurrent explicit-id Add creates one camera",
          "[CameraService][concurrency]") {
    const auto test_base = "/tmp/cosmo_camera_add_concurrency_" +
                           std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_base);
    cosmo::test::ScopedPathOverride path_override(test_base, test_base);

    CameraServiceDependencies mocks;
    ALLOW_CALL(mocks.taskSvc, TaskCreate(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    ALLOW_CALL(mocks.taskSvc, TaskChannelSetUrl(trompeloeil::_, trompeloeil::_));

    CameraServiceImpl svc;
    std::atomic<bool> go{false};
    std::array<cosmo::util::ErrorEnum, 2> results{};
    std::vector<std::thread> adders;
    for (size_t i = 0; i < results.size(); ++i) {
        adders.emplace_back([&, i]() {
            MsgCameraInfo config;
            config.videoChannelId = "fixed-camera-id";
            config.channelName    = "camera";
            config.url            = "rtsp://127.0.0.1:1/test";
            config.channelType    = MsgCameraType::MsgCameraTypeLive;
            std::string id;
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            results[i] = svc.Add(config, id);
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& adder : adders) {
        adder.join();
    }

    const auto success_count   = std::count(results.begin(), results.end(), cosmo::util::ErrorEnum::Success);
    const auto duplicate_count = std::count(results.begin(), results.end(), cosmo::util::ErrorEnum::IDExist);
    REQUIRE(success_count == 1);
    REQUIRE(duplicate_count == 1);

    size_t total = 0;
    auto cameras = svc.Query("", -1, 1, 10, total);
    REQUIRE(total == 1);
    REQUIRE(cameras.size() == 1);

    REQUIRE_CALL(mocks.taskSvc, TaskStop("fixed-camera-id-ChannelTask")).RETURN(true);
    REQUIRE_CALL(mocks.taskSvc, TaskDelete("fixed-camera-id-ChannelTask"))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_NOTHROW(svc.Stop());
    REQUIRE_NOTHROW(svc.Stop());

    std::filesystem::remove_all(test_base);
}

// ============================================================
// 生命周期完整测试 (依赖 DI 注入)
// ============================================================

TEST_CASE("CameraServiceImpl: Full lifecycle (Add Query Update Delete)", "[CameraService]") {
    std::string testBaseDir = "/tmp/cosmo_camera_test_" +
                              std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(testBaseDir);

    cosmo::test::ScopedPathOverride path_override(testBaseDir, testBaseDir);
    CameraServiceDependencies mocks;

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
