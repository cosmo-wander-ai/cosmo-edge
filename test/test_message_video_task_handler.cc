// Unit tests for MessageVideoTaskHandler — all 17 Handle() overloads
// with normal (success) and error paths = 34 test cases.

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "api/MessageVideoTaskHandler.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockCameraService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockTaskService.h"
#include "util/ErrorCode.h"

namespace {

// Helper: create handler wired to MockServiceRegistry mocks
struct TestFixture {
    cosmo::test::MockServiceRegistry mocks;
    cosmo::MessageVideoTaskHandler handler;

    TestFixture()
        : handler(static_cast<cosmo::service::ICameraTaskConfig&>(mocks.cameraSvc),
                  static_cast<cosmo::service::IAlgorithmQuery&>(mocks.algSvc),
                  static_cast<cosmo::service::ICameraDeviceCrud&>(mocks.cameraSvc),
                  static_cast<cosmo::service::ITaskQuery&>(mocks.taskSvc)) {}
};

}  // namespace

// ============================================================================
// 1. ModifyParam
// ============================================================================
TEST_CASE("VideoTaskHandler: ModifyParam success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgModifyParamRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: ModifyParam error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgModifyParamRecv recv;
    recv.channelId = "ch1";

    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 2. QueryParam
// ============================================================================
TEST_CASE("VideoTaskHandler: QueryParam success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQueryParamRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: QueryParam error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQueryParamRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 3. ModifyArea
// ============================================================================
TEST_CASE("VideoTaskHandler: ModifyArea success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgModifyAreaRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc,
                 ModifyTaskArea(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: ModifyArea error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgModifyAreaRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc,
                 ModifyTaskArea(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 4. QueryArea
// ============================================================================
TEST_CASE("VideoTaskHandler: QueryArea success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQueryAreaRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc,
                 QueryTaskArea(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: QueryArea error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQueryAreaRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc,
                 QueryTaskArea(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 5. ModifyStrategy
// ============================================================================
TEST_CASE("VideoTaskHandler: ModifyStrategy success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgModifyStrategyRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";
    recv.scheduleId  = "sched1";

    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: ModifyStrategy error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgModifyStrategyRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 6. QueryStrategy
// ============================================================================
TEST_CASE("VideoTaskHandler: QueryStrategy success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQueryStrategyRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: QueryStrategy error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQueryStrategyRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 7. SwitchTask
// ============================================================================
TEST_CASE("VideoTaskHandler: SwitchTask success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSwitchTaskRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";
    recv.enable      = 1;

    REQUIRE_CALL(f.mocks.cameraSvc, SwitchTask(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: SwitchTask error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSwitchTaskRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc, SwitchTask(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 8. BatchSwitchTask
// ============================================================================
TEST_CASE("VideoTaskHandler: BatchSwitchTask all success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgBatchSwitchTaskRecv recv;
    cosmo::VideoTask::MsgTaskSwitch t1;
    t1.id          = "t1";
    t1.channelId   = "ch1";
    t1.algorithmId = "alg1";
    t1.enable      = 1;
    recv.tasks.push_back(t1);

    REQUIRE_CALL(f.mocks.cameraSvc, SwitchTask(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.failedList.empty());
}

TEST_CASE("VideoTaskHandler: BatchSwitchTask partial failure", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgBatchSwitchTaskRecv recv;
    cosmo::VideoTask::MsgTaskSwitch t1;
    t1.id          = "t1";
    t1.channelId   = "ch1";
    t1.algorithmId = "alg1";
    t1.enable      = 1;
    cosmo::VideoTask::MsgTaskSwitch t2;
    t2.id          = "t2";
    t2.channelId   = "ch2";
    t2.algorithmId = "alg2";
    t2.enable      = 0;
    recv.tasks.push_back(t1);
    recv.tasks.push_back(t2);

    REQUIRE_CALL(f.mocks.cameraSvc,
                 SwitchTask(trompeloeil::eq(std::string("ch1")), trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc,
                 SwitchTask(trompeloeil::eq(std::string("ch2")), trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.failedList.size() == 1);
    REQUIRE(ret.resData.failedList[0].id == "t2");
}

// ============================================================================
// 9. QuerySwitch
// ============================================================================
TEST_CASE("VideoTaskHandler: QuerySwitch success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQuerySwitchRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc, QuerySwitch(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .SIDE_EFFECT(_3 = true)
        .RETURN(cosmo::util::ErrorEnum::Success);
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
    REQUIRE(ret.resData.enable == 1);
}

TEST_CASE("VideoTaskHandler: QuerySwitch error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgQuerySwitchRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc, QuerySwitch(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
    REQUIRE(ret.resData.enable == 0);
}

// ============================================================================
// 10. SaveOrUpdate
// ============================================================================
TEST_CASE("VideoTaskHandler: SaveOrUpdate success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSaveOrUpdateRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, SwitchTask(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: SaveOrUpdate fails at ModifyParam", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSaveOrUpdateRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    // ModifyTaskStrategy and SwitchTask should NOT be called
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 11. SelectConfigByAlgorithmId
// ============================================================================
TEST_CASE("VideoTaskHandler: SelectConfigByAlgorithmId success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSelectConfigByAlgorithmIdRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.algSvc, GetMetaData(trompeloeil::_)).RETURN("{}");
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc,
                 QueryTaskArea(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, QuerySwitch(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .SIDE_EFFECT(_3 = true)
        .RETURN(cosmo::util::ErrorEnum::Success);

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.algorithmMetadata == "{}");
    REQUIRE(ret.resData.taskEnableStatus == 1);
}

TEST_CASE("VideoTaskHandler: SelectConfigByAlgorithmId disabled task", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSelectConfigByAlgorithmIdRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.algSvc, GetMetaData(trompeloeil::_)).RETURN("meta");
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc,
                 QueryTaskArea(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, QuerySwitch(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .SIDE_EFFECT(_3 = false)
        .RETURN(cosmo::util::ErrorEnum::Success);

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.taskEnableStatus == 0);
}

// ============================================================================
// 12. SelectAllAlgorithmInfo
// ============================================================================
TEST_CASE("VideoTaskHandler: SelectAllAlgorithmInfo success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSelectAllAlgorithmInfoRecv recv;
    recv.channelId = "ch1";

    cosmo::service::camera::CameraTaskDto dto;
    dto.algorithmCode = "code1";
    REQUIRE_CALL(f.mocks.cameraSvc, GetTasks(trompeloeil::_))
        .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{dto});
    REQUIRE_CALL(f.mocks.algSvc, Query(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::service::algorithm::AlgorithmPacketInfo>{});

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.algorithmIds.size() == 1);
    REQUIRE(ret.resData.algorithmIds[0] == "code1");
}

TEST_CASE("VideoTaskHandler: SelectAllAlgorithmInfo empty", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSelectAllAlgorithmInfoRecv recv;
    recv.channelId = "ch1";

    REQUIRE_CALL(f.mocks.cameraSvc, GetTasks(trompeloeil::_))
        .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{});
    REQUIRE_CALL(f.mocks.algSvc, Query(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_,
                                       trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::service::algorithm::AlgorithmPacketInfo>{});

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.algorithmIds.empty());
    REQUIRE(ret.resData.algorithmList.empty());
}

// ============================================================================
// 13. ListChannel
// ============================================================================
TEST_CASE("VideoTaskHandler: ListChannel with matching tasks", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgListChannelRecv recv;
    recv.algorithmId = "alg1";

    cosmo::MsgCameraInfo cam;
    cam.videoChannelId = "ch1";
    cam.channelName    = "Camera1";

    cosmo::service::camera::CameraTaskDto dto;
    dto.algorithmCode = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc,
                 Query(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::MsgCameraInfo>{cam});
    REQUIRE_CALL(f.mocks.cameraSvc, GetTasks(trompeloeil::_))
        .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{dto});

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
    REQUIRE(ret.resData.size() == 1);
    REQUIRE(ret.resData[0].channelName == "Camera1");
}

TEST_CASE("VideoTaskHandler: ListChannel no matching tasks", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgListChannelRecv recv;
    recv.algorithmId = "alg_nonexist";

    cosmo::MsgCameraInfo cam;
    cam.videoChannelId = "ch1";

    cosmo::service::camera::CameraTaskDto dto;
    dto.algorithmCode = "alg_other";

    REQUIRE_CALL(f.mocks.cameraSvc,
                 Query(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::MsgCameraInfo>{cam});
    REQUIRE_CALL(f.mocks.cameraSvc, GetTasks(trompeloeil::_))
        .RETURN(std::vector<cosmo::service::camera::CameraTaskDto>{dto});

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.empty());
}

// ============================================================================
// 14. ApplyParamsBatch
// ============================================================================
TEST_CASE("VideoTaskHandler: ApplyParamsBatch all success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgApplyParamsBatchRecv recv;
    recv.algorithmId      = "alg1";
    recv.targetChannelIds = {"ch1"};

    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskParam(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, ModifyTaskStrategy(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, SwitchTask(trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
    REQUIRE(ret.resData.failedList.empty());
}

TEST_CASE("VideoTaskHandler: ApplyParamsBatch partial failure", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgApplyParamsBatchRecv recv;
    recv.algorithmId      = "alg1";
    recv.targetChannelIds = {"ch1", "ch2"};

    // ch1 succeeds fully
    REQUIRE_CALL(f.mocks.cameraSvc,
                 ModifyTaskParam(trompeloeil::eq(std::string("ch1")), trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc,
                 ModifyTaskStrategy(trompeloeil::eq(std::string("ch1")), trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc,
                 SwitchTask(trompeloeil::eq(std::string("ch1")), trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);

    // ch2 fails at ModifyTaskParam
    REQUIRE_CALL(f.mocks.cameraSvc,
                 ModifyTaskParam(trompeloeil::eq(std::string("ch2")), trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
    REQUIRE(ret.resData.failedList.size() == 1);
    REQUIRE(ret.resData.failedList[0].id == "ch2");
}

// ============================================================================
// 15. Delete
// ============================================================================
TEST_CASE("VideoTaskHandler: Delete success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgDeleteRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc, DeleteTask(trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: Delete error", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgDeleteRecv recv;

    REQUIRE_CALL(f.mocks.cameraSvc, DeleteTask(trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::CameraNotExist);
}

// ============================================================================
// 16. BatchDelete
// ============================================================================
TEST_CASE("VideoTaskHandler: BatchDelete all success", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgBatchDeleteRecv recv;
    cosmo::VideoTask::MsgChannelTask t1;
    t1.id          = "t1";
    t1.channelId   = "ch1";
    t1.algorithmId = "alg1";
    recv.tasks.push_back(t1);

    REQUIRE_CALL(f.mocks.cameraSvc, DeleteTask(trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.failedList.empty());
}

TEST_CASE("VideoTaskHandler: BatchDelete all fail", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgBatchDeleteRecv recv;
    cosmo::VideoTask::MsgChannelTask t1;
    t1.id          = "t1";
    t1.channelId   = "ch1";
    t1.algorithmId = "alg1";
    cosmo::VideoTask::MsgChannelTask t2;
    t2.id          = "t2";
    t2.channelId   = "ch2";
    t2.algorithmId = "alg2";
    recv.tasks.push_back(t1);
    recv.tasks.push_back(t2);

    REQUIRE_CALL(f.mocks.cameraSvc, DeleteTask(trompeloeil::_, trompeloeil::_))
        .TIMES(2)
        .RETURN(cosmo::util::ErrorEnum::CameraNotExist);
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.failedList.size() == 2);
}

// ============================================================================
// 17. RunningDetail
// ============================================================================
TEST_CASE("VideoTaskHandler: RunningDetail with tasks", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgRunningDetailRecv recv;
    recv.tasks = {"task1"};

    cosmo::TaskStatus ts;
    ts.channelId = "ch1";
    ts.taskId    = "task1";
    // Add enough queue statuses to pass the kMinActionStatusCount filter (> 2)
    cosmo::AlgActionDataQueueStatusDto qs1, qs2, qs3;
    qs1.actionId = "a1";
    qs2.actionId = "a2";
    qs3.actionId = "a3";
    ts.queStatus = {qs1, qs2, qs3};

    REQUIRE_CALL(f.mocks.taskSvc, GetTaskStatus(trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::TaskStatus>{ts});

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.status.size() == 1);
    REQUIRE(ret.resData.status[0].taskId == "task1");
    REQUIRE(ret.resData.status[0].actionStatus.size() == 3);
}

TEST_CASE("VideoTaskHandler: RunningDetail filters tasks with few actions", "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgRunningDetailRecv recv;
    recv.tasks = {"task1"};

    cosmo::TaskStatus ts;
    ts.channelId = "ch1";
    ts.taskId    = "task1";
    // Only 2 queue statuses — should be filtered out (need > 2)
    cosmo::AlgActionDataQueueStatusDto qs1, qs2;
    ts.queStatus = {qs1, qs2};

    REQUIRE_CALL(f.mocks.taskSvc, GetTaskStatus(trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::TaskStatus>{ts});

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.status.empty());
}
