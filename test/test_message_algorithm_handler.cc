// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "AlgorithmPayloadContract.h"
#include "api/MessageAlgorithmHandler.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockCameraService.h"
#include "mock/MockTaskService.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

TEST_CASE("Algorithm query handlers preserve payloads and isolate errors",
          "[algorithm-handler][algorithm-contract]") {
    MockAlgorithmService algorithms;
    MockCameraService cameras;
    MockTaskService tasks;
    MessageAlgorithmHandler handler(algorithms, algorithms, algorithms, cameras, tasks);
    const bool fail   = GENERATE(false, true);
    const bool empty  = GENERATE(false, true);
    const auto result = fail ? util::ErrorEnum::ImageDecodeFailed : util::ErrorEnum::Success;
    std::error_condition error;

    SECTION("detail forwards both parameters") {
        service::algorithm::LayoutDetailResult payload;
        if (!empty) {
            payload.algorithmCode        = "code";
            payload.algorithmName        = "算法";
            payload.algorithmCategory    = "2";
            payload.algorithmUsage       = "1";
            payload.supplier             = "supplier";
            payload.remark               = "remark";
            payload.confVersionId        = "v2";
            payload.algorithmMetadata    = "{\"z\":2, \"a\":1}";
            payload.algorithmProcessdata = "[]";
            payload.atomicList           = "[]";
            payload.configVersionList    = {{"v2", "second", "code", "{}", "", "", 4294967301ULL},
                                            {"v1", "first", "old", "", "[]", "[]", 12}};
        }
        REQUIRE_CALL(algorithms, GetLayoutDetail("id", "/layouts", _))
            .LR_SIDE_EFFECT(_3 = payload)
            .RETURN(result);
        Algorithm::MsgLayoutDetailRecv request;
        request.id          = "id";
        request.filePath    = "/layouts";
        const auto response = handler.Handle(std::move(request), error);
        CHECK(error == result);
        CHECK(nlohmann::json(response.resData) ==
              ((fail || empty) ? EmptyDetailContract() : DetailContract()));
    }
    SECTION("layout list forwards filters") {
        service::algorithm::LayoutListResult payload;
        if (!empty) {
            payload.list = {{"b", "乙", "s2", "2", "d2"}, {"a", "甲", "s1", "1", "d1"}};
        }
        REQUIRE_CALL(algorithms, GetLayoutList("supplier", -1, "/layouts", _))
            .LR_SIDE_EFFECT(_4 = payload)
            .RETURN(result);
        Algorithm::MsgLayoutListRecv request;
        request.supplier       = "supplier";
        request.algorithmUsage = -1;
        request.filePath       = "/layouts";
        const auto response    = handler.Handle(std::move(request), error);
        CHECK(error == result);
        CHECK(nlohmann::json(response.resData) ==
              ((fail || empty) ? nlohmann::json::parse(R"({"list":[]})") : LayoutListContract()));
    }
    SECTION("actions forward usage and path") {
        service::algorithm::AtomicActionListResult payload;
        if (!empty) {
            payload.list = {{"b", "乙", "second", "{\"b\": 2, \"a\":1}", 2, 3},
                            {"a", "甲", "first", "[]", 1, 4}};
        }
        REQUIRE_CALL(algorithms, GetAtomicActionList(-1, "/actions", _))
            .LR_SIDE_EFFECT(_3 = payload)
            .RETURN(result);
        Algorithm::MsgAtomicActionListRecv request;
        request.actionUsage = -1;
        request.filePath    = "/actions";
        const auto response = handler.Handle(std::move(request), error);
        CHECK(error == result);
        CHECK(nlohmann::json(response.resData) ==
              ((fail || empty) ? nlohmann::json::parse(R"({"list":[]})") : ActionListContract()));
    }
}
