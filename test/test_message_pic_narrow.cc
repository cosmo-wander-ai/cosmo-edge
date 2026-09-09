// Image handlers exercise real task behavior with only narrow service registrations.
#include <future>

#include "api/MessageHandler.h"
#include "catch_amalgamated.hpp"
#include "mock/MockAlgorithmService.h"
#include "mock/MockTaskService.h"
#include "mock/MockVideoFrameCodec.h"
#include "service/media/impl/PicTaskServiceImpl.h"
#include "support/ScopedServiceOverride.h"
#include "util/Keys.h"

using namespace cosmo;
using namespace cosmo::service;
using namespace cosmo::test;
using trompeloeil::_;

TEST_CASE("MessageHandler: narrow picture detect creates and cancels a real empty task",
          "[MessageHandler][PicNarrow]") {
    MockAlgorithmService algorithms;
    MockTaskService tasks;
    ScopedServiceOverride<IAlgorithmQuery> algorithm_query{algorithms};
    ScopedServiceOverride<ITaskLifecycle> task_lifecycle{tasks};
    PicTaskServiceImpl pictures;
    ScopedServiceOverride<IPicTaskDetect> detect{pictures};
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IPicTaskService>());
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IPicTaskQuery>());
    auto algorithm           = std::make_shared<ActionAlg>();
    algorithm->algorithmCode = "empty-workflow";
    REQUIRE_CALL(algorithms, GetAlgorithm("empty-workflow")).RETURN(algorithm);
    REQUIRE_CALL(tasks, RecordClearTaskData("empty-workflow"));
    REQUIRE_CALL(tasks, RecordTaskAction("empty-workflow", algorithm));

    MessageHandler handler;
    std::error_condition error;
    MsgPTaskCreateRecv create;
    create.algorithmCode = "empty-workflow";
    handler.Handle(std::move(create), error);
    REQUIRE_FALSE(error);
    REQUIRE(pictures.QueryTasks(true) == std::vector<std::string>{"empty-workflow"});
    MsgPTaskCancleRecv cancel;
    cancel.algorithmCode = "empty-workflow";
    cancel.mvDebug       = key::DEBUG_STRING;
    handler.Handle(std::move(cancel), error);
    CHECK_FALSE(error);
    CHECK(pictures.TaskCount() == 0);
}

TEST_CASE("MessageHandler: unknown image and empty batch require only picture detect",
          "[MessageHandler][PicNarrow]") {
    PicTaskServiceImpl pictures;
    ScopedServiceOverride<IPicTaskDetect> detect{pictures};
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IPicTaskService>());
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IPicTaskQuery>());
    MessageHandler handler;
    std::error_condition error;
    SECTION("Unknown single image keeps the original error and response defaults") {
        MsgPTaskDetectPicRecv request;
        request.algorithmCode = "unknown";
        auto response         = handler.Handle(std::move(request), error);
        CHECK(error == util::ErrorEnum::NotCreated);
        CHECK(response.resData.algorithmCode == "unknown");
        CHECK_FALSE(response.resData.timestamp.empty());
        CHECK(response.resData.targetList.empty());
        CHECK(response.resData.areaList.empty());
        CHECK(response.resData.fullPicture.empty());
    }
    SECTION("Empty configuration reaches decoding without a query registration") {
        MockVideoFrameCodec codec;
        ScopedServiceOverride<IVideoFrameCodec> frame_codec{codec};
        REQUIRE(pictures.TaskCreate("known", std::make_shared<ActionAlg>()) == util::ErrorEnum::Success);
        REQUIRE(pictures.TaskStart("known"));
        REQUIRE_CALL(codec, DecodeJpeg(_)).RETURN(nullptr);
        MsgPTaskDetectPicRecv request;
        request.algorithmCode = "known";
        request.imageData     = {1};
        auto response         = handler.Handle(std::move(request), error);
        CHECK(error == util::ErrorEnum::ImageDecodeFailed);
        CHECK(response.resData.algorithmCode == "known");
    }
    SECTION("An empty batch keeps its empty result") {
        auto response = handler.Handle(MsgDetectRecv{}, error);
        CHECK_FALSE(error);
        CHECK(response.data.result.empty());
    }
}

TEST_CASE("MessageHandler: picture query stores configuration before image decoding",
          "[MessageHandler][PicNarrow]") {
    const bool use_fallback = GENERATE(false, true);
    PicTaskServiceImpl pictures;
    ScopedServiceOverride<IPicTaskDetect> detect{pictures};
    ScopedServiceOverride<IPicTaskQuery> query{pictures};
    MockVideoFrameCodec codec;
    ScopedServiceOverride<IVideoFrameCodec> frame_codec{codec};
    REQUIRE_FALSE(ServiceRegistry::Instance().Has<IPicTaskService>());
    const std::string task_id = use_fallback ? "algorithm" : "explicit-task";
    REQUIRE(pictures.TaskCreate(task_id, std::make_shared<ActionAlg>()) == util::ErrorEnum::Success);
    REQUIRE(pictures.TaskStart(task_id));
    MsgPTaskDetectPicRecv request;
    request.taskId        = use_fallback ? "" : task_id;
    request.algorithmCode = "algorithm";
    request.imageData     = {1, 2, 3};
    MsgTaskArea area;
    area.areaId = "configured-before-decode";
    request.taskConfig.areas.push_back(area);
    bool observed_saved_config    = false;
    const auto check_saved_config = [&]() {
        // DetectPic holds a shared lock. Read from another thread to avoid recursive locking.
        auto snapshot = std::async(std::launch::async, [&]() {
                            MsgTaskConfig saved;
                            const bool found = pictures.GetTaskParam(task_id, saved);
                            return std::make_pair(found, saved);
                        }).get();
        REQUIRE(snapshot.first);
        REQUIRE(snapshot.second.areas.size() == 1);
        CHECK(snapshot.second.areas[0].areaId == "configured-before-decode");
        observed_saved_config = true;
    };
    REQUIRE_CALL(codec, DecodeJpeg(std::vector<uint8_t>{1, 2, 3}))
        .LR_SIDE_EFFECT(check_saved_config())
        .RETURN(nullptr);
    MessageHandler handler;
    std::error_condition error;
    auto response = handler.Handle(std::move(request), error);
    CHECK(observed_saved_config);
    CHECK(error == util::ErrorEnum::ImageDecodeFailed);
    CHECK(response.resData.algorithmCode == "algorithm");
    CHECK(response.resData.targetList.empty());
}
