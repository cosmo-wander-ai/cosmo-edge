#include "catch_amalgamated.hpp"
// Unit tests for task orchestration logic (ProcessTaskCreate/ProcessTaskCancel)
// in TaskServiceImpl.  We instantiate the *real* TaskServiceImpl and verify the
// debug-mode guard: when mvDebug is empty AND GetHaveManager() returns false,
// errc must be set to MvDebugModel.
//
// MockServiceRegistry already registers mock IAppInfoService with
// GetHaveManager() returning false by default, which is exactly the
// precondition for the guard to trigger.

#include "service/task/impl/TaskServiceImpl.h"
#include "test_mock_services.h"

using namespace cosmo::service;

TEST_CASE("TaskServiceImpl: ProcessTaskCancel rejects non-debug without manager", "[task-service]") {
    cosmo::test::MockServiceRegistry mocks;

    TaskServiceImpl sut;

    cosmo::MsgTaskCancleRecv data;
    data.taskId  = "task1";
    data.mvDebug = "";  // Not debug mode
    std::error_condition errc;

    sut.ProcessTaskCancel(data, errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::MvDebugModel);
}

TEST_CASE("TaskServiceImpl: ProcessTaskCreate rejects non-debug without manager", "[task-service]") {
    cosmo::test::MockServiceRegistry mocks;

    TaskServiceImpl sut;

    cosmo::MsgTaskCreateRecv data;
    data.videoChannelId = "ch1";
    data.taskId         = "task1";
    data.algorithmCode  = "alg_001";
    data.mvDebug        = "";  // Not debug mode
    std::error_condition errc;

    sut.ProcessTaskCreate(data, errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::MvDebugModel);
}
