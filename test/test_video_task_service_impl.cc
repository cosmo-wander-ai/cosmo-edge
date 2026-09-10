#include "catch_amalgamated.hpp"
// Unit tests for task orchestration logic (ProcessTaskCreate/ProcessTaskCancel)
// in TaskServiceImpl.  We instantiate the *real* TaskServiceImpl and verify the
// debug-mode guard: when mvDebug is empty AND GetHaveManager() returns false,
// errc must be set to MvDebugModel.
//
#include "mock/MockActionService.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockClientMessageService.h"
#include "service/task/impl/TaskServiceImpl.h"
#include "support/MockDefaults.h"
#include "support/ScopedServiceOverride.h"
#include "util/Keys.h"

using namespace cosmo::service;

namespace {

struct UnmanagedAppDependency {
    cosmo::test::MockAppInfoService appInfoSvc;
    cosmo::test::NamedExpectations expectations;
    cosmo::test::ScopedServiceOverride<IAppInfoService> appInfo{appInfoSvc};

    UnmanagedAppDependency() {
        expectations.push_back(NAMED_ALLOW_CALL(appInfoSvc, GetHaveManager()).RETURN(false));
    }
};

struct TaskCreateDependencies {
    cosmo::test::MockActionService actionSvc;
    cosmo::test::MockClientMessageService clientMsgSvc;
    cosmo::test::ScopedServiceOverride<IActionService> action{actionSvc};
    cosmo::test::ScopedServiceOverride<IClientMessageService> clientMessage{clientMsgSvc};
};

}  // namespace

TEST_CASE("TaskServiceImpl: ProcessTaskCancel rejects non-debug without manager", "[task-service]") {
    UnmanagedAppDependency mocks;

    TaskServiceImpl sut;

    cosmo::MsgTaskCancleRecv data;
    data.taskId  = "task1";
    data.mvDebug = "";  // Not debug mode
    std::error_condition errc;

    sut.ProcessTaskCancel(data, errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::MvDebugModel);
}

TEST_CASE("TaskServiceImpl: ProcessTaskCreate rejects non-debug without manager", "[task-service]") {
    UnmanagedAppDependency mocks;

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

TEST_CASE("TaskServiceImpl: ProcessTaskCreate passes with debug mode", "[task-service]") {
    TaskCreateDependencies mocks;
    ALLOW_CALL(mocks.actionSvc, GetActionAlg(trompeloeil::_, trompeloeil::_)).RETURN(nullptr);
    ALLOW_CALL(mocks.actionSvc, GetActionAlgByCode(trompeloeil::_)).RETURN(nullptr);
    ALLOW_CALL(mocks.clientMsgSvc, FetchAlgorithmConfig(trompeloeil::_, trompeloeil::_)).RETURN(false);

    TaskServiceImpl sut;

    cosmo::MsgTaskCreateRecv data;
    data.videoChannelId = "ch1";
    data.taskId         = "task1";
    data.algorithmCode  = "alg_001";
    data.mvDebug        = cosmo::key::DEBUG_STRING;  // Debug mode enabled
    std::error_condition errc;

    // With debug mode, it should proceed past the guard
    // (may still fail on camera lookup, but errc != MvDebugModel)
    sut.ProcessTaskCreate(data, errc);
    REQUIRE(errc != cosmo::util::ErrorEnum::MvDebugModel);
    sut.TaskDelete(data.taskId);
}

TEST_CASE("TaskServiceImpl: ProcessTaskCancel passes with debug mode", "[task-service]") {
    TaskServiceImpl sut;

    cosmo::MsgTaskCancleRecv data;
    data.taskId  = "task1";
    data.mvDebug = cosmo::key::DEBUG_STRING;
    std::error_condition errc;

    sut.ProcessTaskCancel(data, errc);
    REQUIRE(errc != cosmo::util::ErrorEnum::MvDebugModel);
}

TEST_CASE("TaskServiceImpl: ProcessTaskCreate with HaveManager passes", "[task-service]") {
    TaskCreateDependencies mocks;
    cosmo::test::MockAppInfoService appInfoSvc;
    cosmo::test::ScopedServiceOverride<IAppInfoService> appInfo(appInfoSvc);
    ALLOW_CALL(appInfoSvc, GetHaveManager()).RETURN(true);
    ALLOW_CALL(mocks.actionSvc, GetActionAlg(trompeloeil::_, trompeloeil::_)).RETURN(nullptr);
    ALLOW_CALL(mocks.actionSvc, GetActionAlgByCode(trompeloeil::_)).RETURN(nullptr);
    ALLOW_CALL(mocks.clientMsgSvc, FetchAlgorithmConfig(trompeloeil::_, trompeloeil::_)).RETURN(false);

    TaskServiceImpl sut;

    cosmo::MsgTaskCreateRecv data;
    data.videoChannelId = "ch1";
    data.taskId         = "task2";
    data.algorithmCode  = "alg_002";
    data.mvDebug        = "";
    std::error_condition errc;

    sut.ProcessTaskCreate(data, errc);
    REQUIRE(errc != cosmo::util::ErrorEnum::MvDebugModel);
    sut.TaskDelete(data.taskId);
}
