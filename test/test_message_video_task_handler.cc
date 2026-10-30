// Unit tests for MessageVideoTaskHandler — all 17 Handle() overloads
// with normal (success) and error paths = 34 test cases.

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include <nlohmann/json.hpp>

#include "api/MessageVideoTaskHandler.h"
#include "media/PreviewPipelineMetrics.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockCameraService.h"
#include "mock/MockTaskService.h"
#include "util/ErrorCode.h"

namespace {

struct VideoTaskHandlerMocks {
    cosmo::test::MockCameraService cameraSvc;
    cosmo::test::MockAlgorithmService algSvc;
    cosmo::test::MockTaskService taskSvc;
};

struct TestFixture {
    VideoTaskHandlerMocks mocks;
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

    REQUIRE_CALL(f.mocks.cameraSvc,
                 SaveOrUpdateTask(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
}

TEST_CASE("VideoTaskHandler: SaveOrUpdate resource rejection is a single atomic call",
          "[video-task-handler]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgSaveOrUpdateRecv recv;
    recv.channelId   = "ch1";
    recv.algorithmId = "alg1";

    REQUIRE_CALL(f.mocks.cameraSvc,
                 SaveOrUpdateTask(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::ResourceLimit);
    f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::ResourceLimit);
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

    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskArea("ch1", "alg1", trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc,
                 SaveOrUpdateTask(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
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

    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskArea(trompeloeil::_, "alg1", trompeloeil::_, trompeloeil::_))
        .TIMES(2)
        .RETURN(cosmo::util::ErrorEnum::Success);
    // ch1 succeeds atomically
    REQUIRE_CALL(f.mocks.cameraSvc, SaveOrUpdateTask(trompeloeil::eq(std::string("ch1")), trompeloeil::_,
                                                     trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);

    // ch2 is rejected without a partial task update
    REQUIRE_CALL(f.mocks.cameraSvc, SaveOrUpdateTask(trompeloeil::eq(std::string("ch2")), trompeloeil::_,
                                                     trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::ResourceLimit);

    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
    REQUIRE(ret.resData.failedList.size() == 1);
    REQUIRE(ret.resData.failedList[0].id == "ch2");
}

TEST_CASE("VideoTaskHandler: ApplyParamsBatch preserves each target's complete regions",
          "[video-task-handler][batch-regions]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgApplyParamsBatchRecv recv;
    recv.algorithmId      = "alg1";
    recv.scheduleId       = "new-schedule";
    recv.targetChannelIds = {"ch1", "ch2", "empty-channel"};

    const auto makeArea = [](const std::string& id, double offset) {
        cosmo::MsgTaskArea area;
        area.areaId     = id;
        area.name       = id + " name";
        area.points     = {{offset, 0.1}, {0.8, 0.1}, {0.8, 0.9}};
        area.linePoints = {{offset, 0.2}, {0.7, 0.6}};
        cosmo::MsgDynamicKeyValue name;
        name.key   = "name";
        name.value = area.name;
        area.params.push_back(name);
        cosmo::MsgTaskArea associated;
        associated.areaId = id + "-associated";
        associated.points = {{offset, 0.3}, {0.6, 0.3}, {0.6, 0.7}};
        area.associatedAreas.push_back(associated);
        return area;
    };
    recv.taskConfig.areas         = {makeArea("source-area", 0.01)};
    recv.taskConfig.shieldedAreas = {makeArea("source-shield", 0.02)};
    cosmo::MsgDynamicKeyValue param;
    param.key   = "param.sensitivity";
    param.value = "7";
    recv.taskConfig.params.push_back(param);

    const std::vector<cosmo::MsgTaskArea> firstAreas{makeArea("first-area", 0.1),
                                                     makeArea("second-area", 0.2)};
    const std::vector<cosmo::MsgTaskArea> firstShields{makeArea("first-shield", 0.3)};
    const std::vector<cosmo::MsgTaskArea> secondAreas{makeArea("other-area", 0.4)};
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskArea("ch1", "alg1", trompeloeil::_, trompeloeil::_))
        .WITH(_3.empty() && _4.empty())
        .SIDE_EFFECT(_3 = firstAreas; _4 = firstShields)
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskArea("ch2", "alg1", trompeloeil::_, trompeloeil::_))
        .WITH(_3.empty() && _4.empty())
        .SIDE_EFFECT(_3 = secondAreas)
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskArea("empty-channel", "alg1", trompeloeil::_, trompeloeil::_))
        .WITH(_3.empty() && _4.empty())
        .RETURN(cosmo::util::ErrorEnum::Success);

    std::vector<cosmo::MsgTaskConfig> saved;
    REQUIRE_CALL(f.mocks.cameraSvc, SaveOrUpdateTask(trompeloeil::_, "alg1", trompeloeil::_, "new-schedule"))
        .TIMES(3)
        .LR_SIDE_EFFECT(saved.push_back(_3))
        .RETURN(cosmo::util::ErrorEnum::Success);
    const auto response = f.handler.Handle(std::move(recv), errc);

    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
    REQUIRE(response.resData.failedList.empty());
    REQUIRE(saved.size() == 3);
    REQUIRE(nlohmann::json(saved[0].areas) == nlohmann::json(firstAreas));
    REQUIRE(nlohmann::json(saved[0].shieldedAreas) == nlohmann::json(firstShields));
    REQUIRE(nlohmann::json(saved[1].areas) == nlohmann::json(secondAreas));
    REQUIRE(saved[1].shieldedAreas.empty());
    REQUIRE(saved[2].areas.empty());
    REQUIRE(saved[2].shieldedAreas.empty());
    for (const auto& config : saved) {
        REQUIRE(config.params.size() == 1);
        REQUIRE(config.params[0].key.ToString() == "param.sensitivity");
        REQUIRE(config.params[0].value.ToString() == "7");
    }
}

TEST_CASE("VideoTaskHandler: ApplyParamsBatch skips unreadable target without writing it",
          "[video-task-handler][batch-regions]") {
    TestFixture f;
    std::error_condition errc;
    cosmo::VideoTask::MsgApplyParamsBatchRecv recv;
    recv.algorithmId      = "alg1";
    recv.targetChannelIds = {"missing", "healthy"};
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskArea("missing", "alg1", trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::TaskNotExist);
    FORBID_CALL(f.mocks.cameraSvc,
                SaveOrUpdateTask("missing", trompeloeil::_, trompeloeil::_, trompeloeil::_));
    REQUIRE_CALL(f.mocks.cameraSvc, QueryTaskArea("healthy", "alg1", trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);
    REQUIRE_CALL(f.mocks.cameraSvc, SaveOrUpdateTask("healthy", "alg1", trompeloeil::_, trompeloeil::_))
        .RETURN(cosmo::util::ErrorEnum::Success);

    const auto response = f.handler.Handle(std::move(recv), errc);
    REQUIRE(errc == cosmo::util::ErrorEnum::Success);
    REQUIRE(response.resData.failedList.size() == 1);
    REQUIRE(response.resData.failedList[0].id == "missing");
    REQUIRE(response.resData.failedList[0].resCode == static_cast<int>(cosmo::util::ErrorEnum::TaskNotExist));
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

TEST_CASE("RunningDetail returns legacy action timings only for explicitly requested tasks",
          "[video-task-handler][metrics]") {
    TestFixture f;
    cosmo::TaskStatus selected;
    selected.channelId      = "channel1";
    selected.algorithmId    = "algorithm1";
    selected.taskId         = "task1";
    selected.queStatus      = {cosmo::AlgActionDataQueueStatusDto{}, cosmo::AlgActionDataQueueStatusDto{},
                               cosmo::AlgActionDataQueueStatusDto{}};
    cosmo::TaskStatus other = selected;
    other.taskId            = "task2";
    other.channelId         = "channel2";
    REQUIRE_CALL(f.mocks.taskSvc, GetTaskStatus(trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::TaskStatus>{selected, other});

    cosmo::util::DurationStatInfo alarm;
    alarm.name        = "internal-node-name";
    alarm.duration_ns = 24'000'000;
    alarm.count       = 3;
    cosmo::util::DurationStatInfo empty;
    empty.name           = "internal-empty";
    using NamedDurations = std::vector<std::pair<std::string, cosmo::util::DurationStatInfo>>;
    REQUIRE_CALL(f.mocks.taskSvc, GetTaskActionDurations("task1", 5000))
        .RETURN(NamedDurations{{"Alarm", alarm}, {"AreaAlarm", empty}});

    cosmo::VideoTask::MsgRunningDetailRecv recv;
    recv.tasks                  = {"task1"};
    recv.includeActionDurations = true;
    nlohmann::json request      = recv;
    CHECK(request.get<cosmo::VideoTask::MsgRunningDetailRecv>().includeActionDurations);
    request.erase("includeActionDurations");
    CHECK_FALSE(request.get<cosmo::VideoTask::MsgRunningDetailRecv>().includeActionDurations);

    std::error_condition errc;
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.status.size() == 2);
    const auto& timings = ret.resData.status[0].actionDurationInfos;
    REQUIRE(timings.size() == 2);
    CHECK(timings[0].name == "Alarm");
    CHECK(timings[0].durationCount == 3);
    CHECK(timings[0].durationAvgUs == 8000);
    CHECK(timings[1].name == "AreaAlarm");
    CHECK(timings[1].durationCount == 0);
    CHECK(ret.resData.status[1].actionDurationInfos.empty());

    nlohmann::json response = ret.resData.status[0];
    REQUIRE(response["actionDurationInfos"].size() == 2);
    CHECK(response.get<cosmo::MsgTaskStatus>().actionDurationInfos[0].name == "Alarm");
    response.erase("actionDurationInfos");
    CHECK(response.get<cosmo::MsgTaskStatus>().actionDurationInfos.empty());
}

TEST_CASE("RunningDetail does not request action timings for unfiltered discovery",
          "[video-task-handler][metrics]") {
    TestFixture f;
    cosmo::TaskStatus task;
    task.taskId    = "task1";
    task.queStatus = {cosmo::AlgActionDataQueueStatusDto{}, cosmo::AlgActionDataQueueStatusDto{},
                      cosmo::AlgActionDataQueueStatusDto{}};
    REQUIRE_CALL(f.mocks.taskSvc, GetTaskStatus(trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::TaskStatus>{task});
    FORBID_CALL(f.mocks.taskSvc, GetTaskActionDurations(trompeloeil::_, trompeloeil::_));
    cosmo::VideoTask::MsgRunningDetailRecv recv;
    recv.includeActionDurations = true;
    std::error_condition errc;
    auto ret = f.handler.Handle(std::move(recv), errc);
    REQUIRE(ret.resData.status.size() == 1);
    CHECK(ret.resData.status[0].actionDurationInfos.empty());
}

TEST_CASE(
    "RunningDetail returns separate current-preview timings and optional DTO remains backwards compatible",
    "[video-task-handler][preview][metrics]") {
    TestFixture f;
    auto& metrics      = cosmo::media::GetPreviewPipelineMetrics();
    const auto preview = metrics.BeginTimedPreview("diag-camera", "diag-algorithm");
    metrics.MarkTimedPreviewReady(preview);
    struct PreviewGuard {
        cosmo::media::PreviewPipelineMetrics& metrics;
        cosmo::media::PreviewPipelineMetrics::PreviewToken token;
        ~PreviewGuard() {
            metrics.EndTimedPreview(token);
        }
    } guard{metrics, preview};
    metrics.RecordTimedOsd(preview, 2'000'000);
    metrics.RecordTimedEncode(preview, 7'000'000);
    // Another stream must never leak into this task's result.
    const auto other = metrics.BeginTimedPreview("other-camera", "diag-algorithm");
    metrics.MarkTimedPreviewReady(other);
    PreviewGuard otherGuard{metrics, other};
    metrics.RecordTimedOsd(other, 99'000'000);

    cosmo::TaskStatus ts;
    ts.channelId   = "diag-camera";
    ts.algorithmId = "diag-algorithm";
    ts.taskId      = "diag-task";
    ts.queStatus   = {cosmo::AlgActionDataQueueStatusDto{}, cosmo::AlgActionDataQueueStatusDto{},
                      cosmo::AlgActionDataQueueStatusDto{}};
    REQUIRE_CALL(f.mocks.taskSvc, GetTaskStatus(trompeloeil::_, trompeloeil::_))
        .RETURN(std::vector<cosmo::TaskStatus>{ts});

    std::error_condition errc;
    auto ret = f.handler.Handle(cosmo::VideoTask::MsgRunningDetailRecv{}, errc);
    REQUIRE(ret.resData.status.size() == 1);
    const auto& status = ret.resData.status.front();
    REQUIRE(status.previewDurationInfos.size() == 2);
    CHECK(status.previewDurationInfos[0].name == "OSD");
    CHECK(status.previewDurationInfos[0].durationAvgUs == 2000);
    CHECK(status.previewDurationInfos[1].name == "Encode");
    CHECK(status.previewDurationInfos[1].durationAvgUs == 7000);
    nlohmann::json json = status;
    REQUIRE(json["previewDurationInfos"].size() == 2);
    CHECK(json.get<cosmo::MsgTaskStatus>().previewDurationInfos.size() == 2);
    json.erase("previewDurationInfos");
    CHECK(json.get<cosmo::MsgTaskStatus>().previewDurationInfos.empty());
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
