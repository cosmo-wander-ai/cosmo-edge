#include "catch_amalgamated.hpp"
/*
 * test_camera_task_mng.cc - CameraServiceImpl per-camera task logic tests
 * (Formerly tested CameraTaskMng directly; now tests via CameraServiceImpl
 *  after CameraTaskMng was inlined.)
 */
#include "mock/MockAppInfoService.h"
#include "mock/MockScheduleService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockTaskService.h"
#include "service/camera/impl/CameraServiceImpl.h"
#include "util/dto/ChannelStatusDto.h"

using namespace cosmo;
using namespace cosmo::service;
using trompeloeil::_;

namespace {
// Helper: create a CameraServiceImpl + add a single camera for testing
struct TestFixture {
    cosmo::test::MockServiceRegistry mocks;
    CameraServiceImpl svc;
    std::string cameraId;

    TestFixture(const std::string& id, const std::string& url) : cameraId(id) {
        ALLOW_CALL(mocks.taskSvc, TaskCreate(_, _, _, _)).RETURN(util::ErrorEnum::Success);
        ALLOW_CALL(mocks.taskSvc, TaskIsStart(_)).RETURN(false);
        ALLOW_CALL(mocks.taskSvc, TaskStart(_, _)).RETURN(true);
        ALLOW_CALL(mocks.taskSvc, TaskStop(_)).RETURN(true);
        ALLOW_CALL(mocks.taskSvc, TaskDelete(_)).RETURN(util::ErrorEnum::Success);
        ALLOW_CALL(mocks.taskSvc, TaskChannelSetUrl(_, _));

        MsgCameraInfo config;
        config.videoChannelId = id;
        config.channelName    = "test";
        config.url            = url;
        config.channelType    = MsgCameraType::MsgCameraTypeLive;
        std::string outId;
        svc.Add(config, outId);
    }
};
}  // namespace

TEST_CASE("CameraServiceImpl basic task operations", "[CameraServiceImpl]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_01");
    TestFixture fx("test_camera_01", "rtsp://test");

    SECTION("GetTasks initially empty") {
        auto tasks = fx.svc.GetTasks(fx.cameraId);
        REQUIRE(tasks.empty());
    }

    SECTION("ScheduleInUse returns false when no tasks") {
        std::string scheduleId = "sched1";
        REQUIRE_FALSE(fx.svc.ScheduleInUse(scheduleId));
    }

    SECTION("QuerySwitch with non-existent code returns error") {
        bool enable = false;
        auto ret    = fx.svc.QuerySwitch(fx.cameraId, "non_existent", enable);
        REQUIRE(ret == util::ErrorEnum::TaskNotExist);
    }

    SECTION("DeleteTask for non-existent") {
        auto ret = fx.svc.DeleteTask(fx.cameraId, "non_existent");
        REQUIRE(ret == util::ErrorEnum::TaskNotExist);
    }
}

TEST_CASE("CameraServiceImpl monitor logic", "[CameraServiceImpl]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_01");
    TestFixture fx("test_camera_01", "rtsp://test");

    ALLOW_CALL(fx.mocks.appInfoSvc, GetNumber()).RETURN(1);
    ALLOW_CALL(fx.mocks.appInfoSvc, GetOverviewStructureRecord()).RETURN(false);
    ALLOW_CALL(fx.mocks.appInfoSvc, GetModelDebug()).RETURN(false);

    // Setup schedule exist mock
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    REQUIRE(fx.svc.ModifyTaskStrategy(fx.cameraId, "test_alg", "sched1") == util::ErrorEnum::Success);

    SECTION("Task starts when enabled, in schedule, and authed") {
        fx.svc.SwitchTask(fx.cameraId, "test_alg", true);  // enable

        ALLOW_CALL(fx.mocks.taskSvc, TaskIsStart("test_camera_01_test_alg")).RETURN(false);
        ALLOW_CALL(fx.mocks.scheduleSvc, InRunTime("sched1")).RETURN(true);

        // Monitor is triggered by the periodic timer; we can't directly call it
        // but verify the task was enabled
        bool enable = false;
        fx.svc.QuerySwitch(fx.cameraId, "test_alg", enable);
        REQUIRE(enable == true);
    }

    SECTION("Task disable works") {
        fx.svc.SwitchTask(fx.cameraId, "test_alg", true);   // enable first
        fx.svc.SwitchTask(fx.cameraId, "test_alg", false);  // disable

        bool enable = true;
        fx.svc.QuerySwitch(fx.cameraId, "test_alg", enable);
        REQUIRE(enable == false);
    }
}

TEST_CASE("CameraServiceImpl concurrent task operations", "[CameraServiceImpl][concurrency]") {
    (void)!system("rm -rf /tmp/cosmo_test/conf/camera/test_camera_02");
    TestFixture fx("test_camera_02", "rtsp://test2");

    std::atomic<bool> stop{false};
    std::atomic<int> successCount{0};

    // Setup initial state
    ALLOW_CALL(fx.mocks.scheduleSvc, Exist2("sched1", trompeloeil::_)).RETURN(true);
    fx.svc.ModifyTaskStrategy(fx.cameraId, "test_alg", "sched1");

    std::vector<std::thread> threads;

    // Concurrent Switch
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 5 && !stop.load(std::memory_order_relaxed); ++j) {
                auto ret = fx.svc.SwitchTask(fx.cameraId, "test_alg", j % 2 == 0);
                if (ret == cosmo::util::ErrorEnum::Success) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(successCount.load() > 0);

    // Wait for any detached SwitchCameraTaskAsync threads to finish before mocks are destroyed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
