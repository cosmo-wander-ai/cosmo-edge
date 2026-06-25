#include "catch_amalgamated.hpp"
/*
 * test_api_handler_core.cc - MessageHandler core endpoint unit tests
 *
 * Tests for InterfaceTest, Probe, ViewRoutes, OverviewStructureRecord,
 * and GraphicsMemory handler methods.
 */
#include "api/MessageHandler.h"
#include "mock/MockAppInfoService.h"
#include "mock/MockLiveStreamService.h"
#include "mock/MockServiceRegistry.h"
#include "util/ErrorCode.h"

using namespace cosmo;

TEST_CASE("MessageHandler: InterfaceTest success", "[CoreHandler]") {
    test::MockServiceRegistry mocks;
    MessageHandler handler;

    MsgInterfaceTestRecv req{};
    req.test                  = "hello";
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageHandler: InterfaceTest error trigger", "[CoreHandler]") {
    test::MockServiceRegistry mocks;
    MessageHandler handler;

    MsgInterfaceTestRecv req{};
    req.test                  = "111";  // triggers ParameterLenError
    std::error_condition errc = util::ErrorEnum::Success;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::ParameterLenError);
}

TEST_CASE("MessageHandler: Probe returns empty", "[CoreHandler]") {
    test::MockServiceRegistry mocks;
    MessageHandler handler;

    MsgProbeRecv req{};
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    // Probe is health check, always returns default empty response
    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageHandler: ViewRoutes delegates to service", "[CoreHandler]") {
    test::MockServiceRegistry mocks;
    MessageHandler handler;

    REQUIRE_CALL(mocks.liveStreamSvc, SetViewCounts(4));

    MsgViewRoutesRecv req{};
    req.viewCounts            = 4;
    std::error_condition errc = util::ErrorEnum::Success;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageHandler: OverviewStructureRecord toggle", "[CoreHandler]") {
    test::MockServiceRegistry mocks;
    MessageHandler handler;

    REQUIRE_CALL(mocks.appInfoSvc, SetOverviewStructureRecord(true));
    REQUIRE_CALL(mocks.appInfoSvc, SetOverviewStructureFile(true));
    ALLOW_CALL(mocks.appInfoSvc, GetTaskOverviewDataPath()).RETURN("/data/overview");

    MsgOverviewStructrueRecordRecv req{};
    req.functionSwitch        = true;
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.path == "/data/overview");
}

TEST_CASE("MessageHandler: GraphicsMemory", "[CoreHandler]") {
    test::MockServiceRegistry mocks;
    MessageHandler handler;

    ALLOW_CALL(mocks.appInfoSvc, OutputMallocBuf()).RETURN("mem_debug_info");

    MsgGraphicsMemoryRecv req{};
    req.test                  = "debug";
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.debugMessage == "mem_debug_info");
}
