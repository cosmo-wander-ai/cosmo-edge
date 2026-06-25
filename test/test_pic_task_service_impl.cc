/*
 * test_pic_task_service_impl.cc — PicTaskServiceImpl unit tests
 *
 * Strategy: PicTaskServiceImpl wraps PTaskBase (flow layer), which manages
 * inference-engine-backed actions.  We test:
 *   1. Pure state-management methods (checksum, task count, query).
 *   2. Input-validation guard clauses (empty taskId, etc.).
 *   3. ProcessPTaskCreate / ProcessPTaskCancel orchestration logic that
 *      delegates to ServiceRegistry services (IAlgorithmService,
 *      IVideoTaskRecord, IAppInfoService).
 *
 * Methods that invoke PTaskBase inference (DetectPic, TaskStart, etc.)
 * are tagged [.device] since they require the full inference stack.
 */
#include "catch_amalgamated.hpp"
#include "mock/MockAlgorithmService.h"
#include "mock/MockServiceRegistry.h"
#include "service/media/impl/PicTaskServiceImpl.h"
#include "util/Keys.h"

using namespace cosmo::service;

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("PicTaskServiceImpl: construction and destruction", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    REQUIRE_NOTHROW([]() { PicTaskServiceImpl sut; }());
}

// ============================================================================
// Checksum round-trip
// ============================================================================

TEST_CASE("PicTaskServiceImpl: checksum round-trip", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("Initial checksum is empty") {
        REQUIRE(sut.GetCheckSum().empty());
    }

    SECTION("UpdateCheckSum + GetCheckSum round-trip") {
        sut.UpdateCheckSum("abc123");
        REQUIRE(sut.GetCheckSum() == "abc123");
    }

    SECTION("UpdateCheckSum overwrites previous value") {
        sut.UpdateCheckSum("v1");
        sut.UpdateCheckSum("v2");
        REQUIRE(sut.GetCheckSum() == "v2");
    }

    SECTION("TasksHaveChange returns true when checksums differ") {
        sut.UpdateCheckSum("checksum_A");
        REQUIRE(sut.TasksHaveChange("checksum_B") == true);
    }

    SECTION("TasksHaveChange returns false when checksums match") {
        sut.UpdateCheckSum("same");
        REQUIRE(sut.TasksHaveChange("same") == false);
    }
}

// ============================================================================
// Task count and query (empty state)
// ============================================================================

TEST_CASE("PicTaskServiceImpl: empty state queries", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("TaskCount is 0 on construction") {
        REQUIRE(sut.TaskCount() == 0);
    }

    SECTION("QueryTasks returns empty on construction") {
        REQUIRE(sut.QueryTasks(false).empty());
        REQUIRE(sut.QueryTasks(true).empty());
    }

    SECTION("QueryRealTasks returns empty on construction") {
        REQUIRE(sut.QueryRealTasks(false).empty());
        REQUIRE(sut.QueryRealTasks(true).empty());
    }

    SECTION("GetTaskStatus returns empty on construction") {
        REQUIRE(sut.GetTaskStatus(30).empty());
    }
}

// ============================================================================
// TaskCreate input validation
// ============================================================================

TEST_CASE("PicTaskServiceImpl: TaskCreate input validation", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("Empty taskId returns InvalidParam") {
        auto alg    = std::make_shared<cosmo::ActionAlg>();
        auto result = sut.TaskCreate("", alg);
        REQUIRE(result == cosmo::util::ErrorEnum::InvalidParam);
    }

    SECTION("Null actionAlg causes TaskCreate to return ActionFailed") {
        // PTaskBase::TaskCreate returns nullptr for null actionAlg
        auto result = sut.TaskCreate("task1", nullptr);
        REQUIRE(result == cosmo::util::ErrorEnum::ActionFailed);
    }

    SECTION("Valid taskId with empty workflow succeeds") {
        auto alg    = std::make_shared<cosmo::ActionAlg>();
        auto result = sut.TaskCreate("task_ok", alg);
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.TaskCount() == 1);
    }
}

// ============================================================================
// TaskDelete input validation
// ============================================================================

TEST_CASE("PicTaskServiceImpl: TaskDelete input validation", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("Empty taskId returns InvalidParam") {
        auto result = sut.TaskDelete("");
        REQUIRE(result == cosmo::util::ErrorEnum::InvalidParam);
    }

    SECTION("Non-existent task returns NotInit") {
        auto result = sut.TaskDelete("nonexistent");
        REQUIRE(result == cosmo::util::ErrorEnum::NotInit);
    }

    SECTION("Delete existing task succeeds and decrements count") {
        auto alg = std::make_shared<cosmo::ActionAlg>();
        sut.TaskCreate("del_test", alg);
        REQUIRE(sut.TaskCount() == 1);

        auto result = sut.TaskDelete("del_test");
        REQUIRE(result == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.TaskCount() == 0);
    }
}

// ============================================================================
// TaskDeleteAll
// ============================================================================

TEST_CASE("PicTaskServiceImpl: TaskDeleteAll clears all tasks", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    auto alg1 = std::make_shared<cosmo::ActionAlg>();
    auto alg2 = std::make_shared<cosmo::ActionAlg>();
    sut.TaskCreate("t1", alg1);
    sut.TaskCreate("t2", alg2);
    REQUIRE(sut.TaskCount() == 2);

    sut.TaskDeleteAll();
    REQUIRE(sut.TaskCount() == 0);
    REQUIRE(sut.QueryTasks(false).empty());
}

// ============================================================================
// QueryTasks and QueryRealTasks
// ============================================================================

TEST_CASE("PicTaskServiceImpl: QueryTasks with tasks", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    auto alg = std::make_shared<cosmo::ActionAlg>();
    sut.TaskCreate("algo1-0", alg);
    sut.TaskCreate("algo1-1", alg);
    sut.TaskCreate("algo2-0", alg);

    SECTION("QueryTasks(false) returns all tasks") {
        auto tasks = sut.QueryTasks(false);
        REQUIRE(tasks.size() == 3);
    }

    SECTION("QueryTasks(true) returns only started tasks (none started)") {
        auto tasks = sut.QueryTasks(true);
        REQUIRE(tasks.empty());
    }

    SECTION("QueryRealTasks deduplicates by prefix before '-'") {
        auto realTasks = sut.QueryRealTasks(false);
        // "algo1-0" and "algo1-1" share prefix "algo1", "algo2-0" has prefix "algo2"
        REQUIRE(realTasks.size() == 2);
        // Verify both prefixes are present
        bool hasAlgo1 = std::find(realTasks.begin(), realTasks.end(), "algo1") != realTasks.end();
        bool hasAlgo2 = std::find(realTasks.begin(), realTasks.end(), "algo2") != realTasks.end();
        REQUIRE(hasAlgo1);
        REQUIRE(hasAlgo2);
    }
}

// ============================================================================
// GetTaskParam / SetTaskParam validation
// ============================================================================

TEST_CASE("PicTaskServiceImpl: GetTaskParam / SetTaskParam validation", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("SetTaskParam for non-existent task returns false") {
        cosmo::MsgTaskConfig param;
        REQUIRE(sut.SetTaskParam("no_such_task", param) == false);
    }

    SECTION("GetTaskParam for non-existent task returns false") {
        cosmo::MsgTaskConfig param;
        REQUIRE(sut.GetTaskParam("no_such_task", param) == false);
    }

    SECTION("GetTaskParam returns stored params for existing task") {
        auto alg = std::make_shared<cosmo::ActionAlg>();
        sut.TaskCreate("param_test", alg);

        cosmo::MsgTaskConfig result;
        REQUIRE(sut.GetTaskParam("param_test", result) == true);
    }
}

// ============================================================================
// TaskCreate duplicate handling
// ============================================================================

TEST_CASE("PicTaskServiceImpl: TaskCreate duplicate handling", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    auto alg = std::make_shared<cosmo::ActionAlg>();

    SECTION("Creating the same taskId twice returns Created") {
        auto r1 = sut.TaskCreate("dup", alg);
        REQUIRE(r1 == cosmo::util::ErrorEnum::Success);
        auto r2 = sut.TaskCreate("dup", alg);
        REQUIRE(r2 == cosmo::util::ErrorEnum::Created);
        REQUIRE(sut.TaskCount() == 1);
    }

    SECTION("Creating with updated version replaces the task") {
        auto algV1                 = std::make_shared<cosmo::ActionAlg>();
        algV1->algorithmUpdateTime = "v1";
        auto algV2                 = std::make_shared<cosmo::ActionAlg>();
        algV2->algorithmUpdateTime = "v2";

        auto r1 = sut.TaskCreate("versioned", algV1);
        REQUIRE(r1 == cosmo::util::ErrorEnum::Success);
        auto r2 = sut.TaskCreate("versioned", algV2);
        // Should recreate (delete old + create new) = Success
        REQUIRE(r2 == cosmo::util::ErrorEnum::Success);
        REQUIRE(sut.TaskCount() == 1);
    }
}

// ============================================================================
// ProcessPTaskCreate orchestration
// ============================================================================

TEST_CASE("PicTaskServiceImpl: ProcessPTaskCreate orchestration", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("Succeeds with valid algorithmCode") {
        auto alg           = std::make_shared<cosmo::ActionAlg>();
        alg->algorithmName = "test_alg";
        alg->algorithmCode = "code1";

        REQUIRE_CALL(mocks.algSvc, GetAlgorithm("code1")).RETURN(alg);

        cosmo::MsgPTaskCreateRecv data;
        data.taskId        = "ptask1";
        data.algorithmCode = "code1";
        std::error_condition errc;

        auto result = sut.ProcessPTaskCreate(data, errc);
        REQUIRE_FALSE(errc);
        REQUIRE(sut.TaskCount() == 1);
    }

    SECTION("Empty taskId defaults to algorithmCode") {
        auto alg           = std::make_shared<cosmo::ActionAlg>();
        alg->algorithmCode = "mycode";

        REQUIRE_CALL(mocks.algSvc, GetAlgorithm("mycode")).RETURN(alg);

        cosmo::MsgPTaskCreateRecv data;
        data.taskId        = "";  // empty — should default to algorithmCode
        data.algorithmCode = "mycode";
        std::error_condition errc;

        sut.ProcessPTaskCreate(data, errc);
        REQUIRE_FALSE(errc);

        // Task should be keyed by algorithmCode
        auto tasks = sut.QueryTasks(false);
        REQUIRE(tasks.size() == 1);
        REQUIRE(tasks[0] == "mycode");
    }

    SECTION("Unknown algorithmCode sets ActionAlgLoadFailed error") {
        REQUIRE_CALL(mocks.algSvc, GetAlgorithm("unknown")).RETURN(nullptr);

        cosmo::MsgPTaskCreateRecv data;
        data.taskId        = "ptask_fail";
        data.algorithmCode = "unknown";
        std::error_condition errc;

        sut.ProcessPTaskCreate(data, errc);
        REQUIRE(errc == cosmo::util::ErrorEnum::ActionAlgLoadFailed);
    }
}

// ============================================================================
// ProcessPTaskCancel orchestration
// ============================================================================

TEST_CASE("PicTaskServiceImpl: ProcessPTaskCancel orchestration", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("Cancel existing task succeeds") {
        // Create a task first
        auto alg = std::make_shared<cosmo::ActionAlg>();
        sut.TaskCreate("cancel_me", alg);

        cosmo::MsgPTaskCancleRecv data;
        data.taskId        = "cancel_me";
        data.algorithmCode = "cancel_me";
        data.mvDebug       = cosmo::key::DEBUG_STRING;
        std::error_condition errc;

        sut.ProcessPTaskCancel(data, errc);
        REQUIRE_FALSE(errc);
        REQUIRE(sut.TaskCount() == 0);
    }

    SECTION("Cancel non-existent task does not set error (NotInit is acceptable)") {
        cosmo::MsgPTaskCancleRecv data;
        data.taskId        = "does_not_exist";
        data.algorithmCode = "does_not_exist";
        data.mvDebug       = cosmo::key::DEBUG_STRING;
        std::error_condition errc;

        sut.ProcessPTaskCancel(data, errc);
        // NotInit and Success both count as no error
        REQUIRE_FALSE(errc);
    }

    SECTION("Empty taskId defaults to algorithmCode") {
        auto alg = std::make_shared<cosmo::ActionAlg>();
        sut.TaskCreate("fallback", alg);

        cosmo::MsgPTaskCancleRecv data;
        data.taskId        = "";
        data.algorithmCode = "fallback";
        data.mvDebug       = cosmo::key::DEBUG_STRING;
        std::error_condition errc;

        sut.ProcessPTaskCancel(data, errc);
        REQUIRE_FALSE(errc);
        REQUIRE(sut.TaskCount() == 0);
    }

    SECTION("Cancel without debug flag and without manager returns MvDebugModel") {
        cosmo::MsgPTaskCancleRecv data;
        data.taskId        = "task1";
        data.algorithmCode = "task1";
        data.mvDebug       = "not_debug";
        std::error_condition errc;

        // GetHaveManager defaults to false via MockServiceRegistry
        sut.ProcessPTaskCancel(data, errc);
        REQUIRE(errc == cosmo::util::ErrorEnum::MvDebugModel);
    }
}

// ============================================================================
// DetectPic input validation
// ============================================================================

TEST_CASE("PicTaskServiceImpl: DetectPic input validation", "[PicTaskService]") {
    cosmo::test::MockServiceRegistry mocks;
    PicTaskServiceImpl sut;

    SECTION("Empty taskId returns InvalidParam") {
        cosmo::MsgPTaskDetectPicRecv data;
        cosmo::MsgPTaskDetectPicSend retData;
        auto result = sut.DetectPic("", data, retData);
        REQUIRE(result == cosmo::util::ErrorEnum::InvalidParam);
    }

    SECTION("Non-existent task returns NotCreated") {
        cosmo::MsgPTaskDetectPicRecv data;
        cosmo::MsgPTaskDetectPicSend retData;
        auto result = sut.DetectPic("no_such", data, retData);
        REQUIRE(result == cosmo::util::ErrorEnum::NotCreated);
    }
}
