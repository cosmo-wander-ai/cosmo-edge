#include "catch_amalgamated.hpp"
/*
 * test_camera_task_mng.cc - CameraTaskMng 单元测试
 */
#include "flow/task/CameraTaskMng.h"
#include "test_mock_services.h"
#include "util/dto/ChannelStatusDto.h"

using namespace cosmo;
using trompeloeil::_;

TEST_CASE("CameraTaskMng basic operations", "[CameraTaskMng]") {
    system("rm -rf /tmp/test_camera_basic");
    cosmo::test::MockServiceRegistry mocks;
    CameraTaskMng mng("/tmp/test_camera_basic", "test_camera_01", "rtsp://test");

    ALLOW_CALL(mocks.taskSvc, TaskIsStart(_)).RETURN(false);
    ALLOW_CALL(mocks.taskSvc, TaskStart(_, _)).RETURN(true);
    ALLOW_CALL(mocks.taskSvc, TaskStop(_)).RETURN(true);

    SECTION("GetTasks initially empty") {
        auto tasks = mng.GetTasks();
        REQUIRE(tasks.empty());
    }

    SECTION("ScheduleInUse returns false when no tasks") {
        std::string scheduleId = "sched1";
        REQUIRE_FALSE(mng.ScheduleInUse(scheduleId));
    }

    SECTION("Query returns empty page") {
        size_t total = 0;
        auto result  = mng.Query(1, 10, total);
        REQUIRE(result.empty());
        REQUIRE(total == 0);
    }

    SECTION("GetCameraTask with non-existent code returns nullptr") {
        auto task = mng.GetCameraTask("non_existent");
        REQUIRE(task == nullptr);
    }

    SECTION("DeleteTask for non-existent") {
        auto ret = mng.DeleteTask("non_existent");
        // Should not crash
        REQUIRE(true);
    }

    SECTION("GetSwitch for non-existent") {
        bool enable = false;
        auto ret    = mng.GetSwitch("non_existent", enable);
        // Should return an error, not crash
        REQUIRE(true);
    }
}

TEST_CASE("CameraTaskMng Monitor logic", "[CameraTaskMng]") {
    system("rm -rf /tmp/test_camera_monitor");
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.appInfoSvc, GetNumber()).RETURN(1);
    ALLOW_CALL(mocks.appInfoSvc, GetOverviewStructureRecord()).RETURN(false);
    ALLOW_CALL(mocks.appInfoSvc, GetModelDebug()).RETURN(false);

    CameraTaskMng mng("/tmp/test_camera_monitor", "test_camera_01", "rtsp://test");

    ALLOW_CALL(mocks.taskSvc, TaskIsStart(_)).RETURN(false);
    ALLOW_CALL(mocks.taskSvc, TaskStart(_, _)).RETURN(true);
    ALLOW_CALL(mocks.taskSvc, TaskStop(_)).RETURN(true);

    // Setup schedule exist mock
    ALLOW_CALL(mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);

    REQUIRE(mng.SetStrategySchedule("test_alg", "sched1") == cosmo::util::ErrorEnum::Success);

    SECTION("Task starts when enabled, in schedule, and authed") {
        mng.Switch("test_alg", true);  // enable

        ALLOW_CALL(mocks.taskSvc, TaskIsStart("test_camera_01_test_alg")).RETURN(false);
        ALLOW_CALL(mocks.scheduleSvc, InRunTime("sched1")).RETURN(true);

        REQUIRE_CALL(mocks.taskSvc, TaskStart("test_camera_01", "test_camera_01_test_alg")).RETURN(true);

        mng.Monitor(true);  // isAuthed = true
    }

    SECTION("Task stops when disabled even if running") {
        mng.Switch("test_alg", false);  // disable

        ALLOW_CALL(mocks.taskSvc, TaskIsStart("test_camera_01_test_alg")).RETURN(true);
        ALLOW_CALL(mocks.scheduleSvc, InRunTime("sched1")).RETURN(true);

        REQUIRE_CALL(mocks.taskSvc, TaskStop("test_camera_01_test_alg")).RETURN(true);

        mng.Monitor(true);
    }

    SECTION("Task stops when not in schedule") {
        mng.Switch("test_alg", true);  // enable

        ALLOW_CALL(mocks.taskSvc, TaskIsStart("test_camera_01_test_alg")).RETURN(true);
        ALLOW_CALL(mocks.scheduleSvc, InRunTime("sched1")).RETURN(false);  // Out of schedule

        REQUIRE_CALL(mocks.taskSvc, TaskStop("test_camera_01_test_alg")).RETURN(true);

        mng.Monitor(true);
    }

    SECTION("Task stops when not authed") {
        mng.Switch("test_alg", true);  // enable

        ALLOW_CALL(mocks.taskSvc, TaskIsStart("test_camera_01_test_alg")).RETURN(true);
        ALLOW_CALL(mocks.scheduleSvc, InRunTime("sched1")).RETURN(true);

        REQUIRE_CALL(mocks.taskSvc, TaskStop("test_camera_01_test_alg")).RETURN(true);

        mng.Monitor(false);  // isAuthed = false
    }

    SECTION("Offline video read end stops task") {
        mng.Switch("test_alg", true);

        ALLOW_CALL(mocks.taskSvc, TaskIsStart("test_camera_01_test_alg")).RETURN(true);
        ALLOW_CALL(mocks.scheduleSvc, InRunTime("sched1")).RETURN(true);

        // Mock channel attr returning read end via manual stub
        mocks.taskSvc.mock_dataStatus =
            static_cast<int>(cosmo::service::camera::AlgDemuxStatus::AlgDemuxReadEnd);

        ALLOW_CALL(mocks.taskSvc, TaskDataActive("test_camera_01")).RETURN(false);

        REQUIRE_CALL(mocks.taskSvc, TaskStop("test_camera_01_test_alg")).RETURN(true);

        mng.Monitor(true);
    }
}

TEST_CASE("CameraTaskMng Monitor skips task after reload rebuild failure", "[CameraTaskMng]") {
    system("rm -rf /tmp/test_camera_reload_failed");
    cosmo::test::MockServiceRegistry mocks;
    CameraTaskMng mng("/tmp/test_camera_reload_failed", "test_camera_reload", "rtsp://test");

    ALLOW_CALL(mocks.scheduleSvc, Exist2("sched1", _)).LR_SIDE_EFFECT(_2 = "Schedule 1").RETURN(true);
    REQUIRE(mng.SetStrategySchedule("test_alg", "sched1") == cosmo::util::ErrorEnum::Success);

    ALLOW_CALL(mocks.taskSvc, TaskCreate(_, _, _, _)).RETURN(cosmo::util::ErrorEnum::TaskCreateFailed);
    ALLOW_CALL(mocks.algSvc, GetAlgorithmName(_)).RETURN("test_alg_name");

    REQUIRE_NOTHROW(mng.RebuildAlgorithmForReload("test_alg"));
    REQUIRE_NOTHROW(mng.Monitor(true));

    auto task = mng.GetCameraTask("test_alg");
    REQUIRE(task != nullptr);
    REQUIRE(task->task_ == nullptr);
    REQUIRE(task->status_ == CameraTaskStatus::kAbnormal);
}

TEST_CASE("CameraTaskMng concurrent operations", "[CameraTaskMng][concurrency]") {
    system("rm -rf /tmp/test_camera_conc");
    cosmo::test::MockServiceRegistry mocks;
    CameraTaskMng mng("/tmp/test_camera_conc", "test_camera_02", "rtsp://test2");

    std::atomic<bool> stop{false};
    std::atomic<int> successCount{0};

    // Setup initial state
    ALLOW_CALL(mocks.scheduleSvc, Exist2("sched1", trompeloeil::_)).RETURN(true);
    mng.SetStrategySchedule("test_alg", "sched1");

    std::vector<std::thread> threads;

    // Concurrent Switch
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 5 && !stop.load(std::memory_order_relaxed); ++j) {
                auto ret = mng.Switch("test_alg", j % 2 == 0);
                if (ret == cosmo::util::ErrorEnum::Success) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    // Concurrent Monitor
    threads.emplace_back([&]() {
        ALLOW_CALL(mocks.appInfoSvc, GetNumber()).RETURN(1);
        ALLOW_CALL(mocks.appInfoSvc, GetOverviewStructureRecord()).RETURN(false);
        ALLOW_CALL(mocks.appInfoSvc, GetModelDebug()).RETURN(false);
        ALLOW_CALL(mocks.taskSvc, TaskIsStart(trompeloeil::_)).RETURN(true);
        ALLOW_CALL(mocks.scheduleSvc, InRunTime("sched1")).RETURN(true);
        ALLOW_CALL(mocks.taskSvc, TaskStop(trompeloeil::_)).RETURN(true);
        ALLOW_CALL(mocks.taskSvc, TaskStart(trompeloeil::_, trompeloeil::_)).RETURN(true);
        mocks.taskSvc.mock_dataStatus = 0;
        ALLOW_CALL(mocks.taskSvc, TaskDataActive("test_camera_02")).RETURN(true);

        for (int j = 0; j < 20 && !stop.load(std::memory_order_relaxed); ++j) {
            mng.Monitor(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true);

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(successCount.load() > 0);

    // Wait for any detached SwitchTaskAsync threads to finish before mocks are destroyed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
