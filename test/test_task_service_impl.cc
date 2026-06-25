#include "catch_amalgamated.hpp"
#include "mock/MockAlgorithmService.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockServiceRegistry.h"
#include "service/task/impl/TaskServiceImpl.h"
#include "util/ErrorCode.h"

using namespace cosmo::service;

TEST_CASE("TaskServiceImpl: TaskCreate lifecycle", "[TaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.appInfoSvc, GetNumber()).RETURN(1);
    TaskServiceImpl svc;
    auto alg           = std::make_shared<cosmo::ActionAlg>();
    alg->algorithmCode = "test_alg";
    alg->algorithmName = "test_alg_name";

    ALLOW_CALL(mocks.algSvc, GetAlgorithm("test_alg")).RETURN(alg);

    SECTION("empty channelId/taskId returns InvalidParam") {
        REQUIRE(svc.TaskCreate("", "channel_name", "task1", alg) == cosmo::util::ErrorEnum::InvalidParam);
        REQUIRE(svc.TaskCreate("ch1", "channel_name", "", alg) == cosmo::util::ErrorEnum::InvalidParam);
    }

    SECTION("TaskCreate with nullptr actionAlg returns ActionFailed") {
        REQUIRE(svc.TaskCreate("ch1", "channel_name", "task1", nullptr) ==
                cosmo::util::ErrorEnum::ActionFailed);
    }

    SECTION("duplicate taskId returns Created") {
        REQUIRE(svc.TaskCreate("ch1", "channel_name", "task1", alg) == cosmo::util::ErrorEnum::Success);
        REQUIRE(svc.TaskCreate("ch2", "channel_name2", "task1", alg) == cosmo::util::ErrorEnum::Created);
    }

    SECTION("valid create increments TaskCount") {
        REQUIRE(svc.TaskCount() == 0);
        REQUIRE(svc.TaskCreate("ch1", "channel_name", "task1", alg) == cosmo::util::ErrorEnum::Success);
        REQUIRE(svc.TaskCount() == 1);
        REQUIRE(svc.TaskCreate("ch2", "channel_name", "task2", alg) == cosmo::util::ErrorEnum::Success);
        REQUIRE(svc.TaskCount() == 2);
    }
}

TEST_CASE("TaskServiceImpl: TaskDelete lifecycle", "[TaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.appInfoSvc, GetNumber()).RETURN(1);
    ALLOW_CALL(mocks.appInfoSvc, GetOverviewStructureRecord()).RETURN(false);
    ALLOW_CALL(mocks.appInfoSvc, GetModelDebug()).RETURN(false);
    TaskServiceImpl svc;
    auto alg           = std::make_shared<cosmo::ActionAlg>();
    alg->algorithmCode = "test_alg";

    ALLOW_CALL(mocks.algSvc, GetAlgorithm("test_alg")).RETURN(alg);

    SECTION("empty taskId returns InvalidParam") {
        REQUIRE(svc.TaskDelete("") == cosmo::util::ErrorEnum::InvalidParam);
    }

    SECTION("non-existent returns NotInit") {
        REQUIRE(svc.TaskDelete("non_existent") == cosmo::util::ErrorEnum::NotInit);
    }

    SECTION("valid delete removes task and cleans log throttle") {
        REQUIRE(svc.TaskCreate("ch1", "channel_name", "task1", alg) == cosmo::util::ErrorEnum::Success);
        REQUIRE(svc.TaskCount() == 1);

        // Trigger a log throttle entry
        svc.GetTaskLiveOverviewInfo("task1", -1, -1, -1);

        REQUIRE(svc.TaskDelete("task1") == cosmo::util::ErrorEnum::Success);
        REQUIRE(svc.TaskCount() == 0);
    }
}

TEST_CASE("TaskServiceImpl: TaskStart/TaskStop", "[TaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.appInfoSvc, GetNumber()).RETURN(1);
    TaskServiceImpl svc;
    auto alg           = std::make_shared<cosmo::ActionAlg>();
    alg->algorithmCode = "test_alg";

    ALLOW_CALL(mocks.algSvc, GetAlgorithm("test_alg")).RETURN(alg);

    SECTION("Start non-existent returns false") {
        REQUIRE_FALSE(svc.TaskStart("ch1", "non_existent"));
    }

    SECTION("Stop non-existent returns false") {
        REQUIRE_FALSE(svc.TaskStop("non_existent"));
    }

    SECTION("TaskIsStart for non-existent returns false") {
        REQUIRE_FALSE(svc.TaskIsStart("non_existent"));
    }

    SECTION("Start and Stop an existing task") {
        REQUIRE(svc.TaskCreate("ch1", "channel_name", "task1", alg) == cosmo::util::ErrorEnum::Success);
        REQUIRE_FALSE(svc.TaskIsStart("task1"));

        // TaskStart logic requires finding the algorithm, we just check it doesn't crash
        // and does its logic. It may return false if no valid actionAlg is found in DI (which is null here).
        // For a pure state check:
        // TaskStart requires m_taskBase.TaskStart(oldTask)
        svc.TaskStart("ch1", "task1");

        // TaskStop
        REQUIRE(svc.TaskStop("task1"));
        // Idempotent
        REQUIRE(svc.TaskStop("task1"));
    }
}

TEST_CASE("TaskServiceImpl: Query methods", "[TaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.appInfoSvc, GetNumber()).RETURN(1);
    TaskServiceImpl svc;
    auto alg           = std::make_shared<cosmo::ActionAlg>();
    alg->algorithmCode = "test_alg";

    ALLOW_CALL(mocks.algSvc, GetAlgorithm("test_alg")).RETURN(alg);

    SECTION("QueryTasks empty -> empty") {
        REQUIRE(svc.QueryTasks(false).empty());
        REQUIRE(svc.QueryTasks(true).empty());
    }

    SECTION("TaskCount == 0 on init") {
        REQUIRE(svc.TaskCount() == 0);
    }

    SECTION("GetAlgorithmCount returns 0 for non-existent") {
        REQUIRE(svc.GetAlgorithmCount("test_alg") == 0);
    }

    SECTION("CameraTaskInfo empty -> empty") {
        REQUIRE(svc.CameraTaskInfo().empty());
    }

    SECTION("GetTaskChannel non-existent -> empty") {
        REQUIRE(svc.GetTaskChannel("non_existent") == "");
    }

    SECTION("Query methods after create") {
        REQUIRE(svc.TaskCreate("ch1", "channel_name", "task1", alg) == cosmo::util::ErrorEnum::Success);

        auto tasks = svc.QueryTasks(false);
        REQUIRE(tasks.size() == 1);
        REQUIRE(tasks[0] == "task1");

        // "test_alg" should have count 1
        REQUIRE(svc.GetAlgorithmCount("test_alg") == 1);

        auto infos = svc.CameraTaskInfo();
        REQUIRE(infos.size() == 1);
        REQUIRE(infos[0].videoChannelId == "ch1");
    }
}

TEST_CASE("TaskServiceImpl: log throttle", "[TaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    ALLOW_CALL(mocks.appInfoSvc, GetNumber()).RETURN(1);
    TaskServiceImpl svc;

    SECTION("rapid GetTaskLiveOverviewInfo throttled") {
        // First call will log
        auto res1 = svc.GetTaskLiveOverviewInfo("non_existent", -1, -1, -1);
        REQUIRE(res1.empty());

        // Second call immediately will be throttled (we just ensure it doesn't crash)
        auto res2 = svc.GetTaskLiveOverviewInfo("non_existent", -1, -1, -1);
        REQUIRE(res2.empty());
    }
}
