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
#include "support/ScopedServiceOverride.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"

using namespace cosmo;

TEST_CASE("MessageHandler: InterfaceTest success", "[CoreHandler]") {
    MessageHandler handler;

    MsgInterfaceTestRecv req{};
    req.test                  = "hello";
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageHandler: InterfaceTest error trigger", "[CoreHandler]") {
    MessageHandler handler;

    MsgInterfaceTestRecv req{};
    req.test                  = "111";  // triggers ParameterLenError
    std::error_condition errc = util::ErrorEnum::Success;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::ParameterLenError);
}

TEST_CASE("MessageHandler: Probe returns empty", "[CoreHandler]") {
    MessageHandler handler;

    MsgProbeRecv req{};
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    // Probe is health check, always returns default empty response
    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageHandler: ViewRoutes delegates to service", "[CoreHandler]") {
    test::MockLiveStreamService live_stream;
    test::ScopedServiceOverride<service::ILiveStreamService> registration(live_stream);
    MessageHandler handler;

    REQUIRE_CALL(live_stream, SetViewCounts(4));

    MsgViewRoutesRecv req{};
    req.viewCounts            = 4;
    std::error_condition errc = util::ErrorEnum::Success;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageHandler: OverviewStructureRecord toggle", "[CoreHandler]") {
    test::MockAppInfoService app_info;
    test::ScopedServiceOverride<service::IAppInfoService> registration(app_info);
    MessageHandler handler;

    REQUIRE_CALL(app_info, SetOverviewStructureRecord(true));
    REQUIRE_CALL(app_info, SetOverviewStructureFile(true));
    ALLOW_CALL(app_info, GetTaskOverviewDataPath()).RETURN("/data/overview");

    MsgOverviewStructrueRecordRecv req{};
    req.functionSwitch        = true;
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.path == "/data/overview");
}

TEST_CASE("MessageHandler: GraphicsMemory", "[CoreHandler]") {
    test::MockAppInfoService app_info;
    test::ScopedServiceOverride<service::IMemoryDiag> registration(app_info);
    REQUIRE_FALSE(service::ServiceRegistry::Instance().Has<service::IAppInfoService>());
    MessageHandler handler;

    ALLOW_CALL(app_info, OutputMallocBuf()).RETURN("mem_debug_info");

    MsgGraphicsMemoryRecv req{};
    req.test                  = "debug";
    std::error_condition errc = util::ErrorEnum::Success;
    auto rsp                  = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.debugMessage == "mem_debug_info");
}

TEST_CASE("MessageHandler: overview file rejects path-like task ID", "[CoreHandler][security]") {
    MessageHandler handler;

    MsgQueryTaskOverviewFileRecv req{};
    req.taskId                = "../../outside";
    std::error_condition errc = util::ErrorEnum::Success;

    REQUIRE_THROWS_AS(handler.Handle(std::move(req), errc), util::ErrorMessage);
}

TEST_CASE("MessageHandler: memory pools use only diagnostics", "[CoreHandler][narrow-interface]") {
    test::MockAppInfoService app_info;
    test::ScopedServiceOverride<service::IMemoryDiag> registration(app_info);
    REQUIRE_FALSE(service::ServiceRegistry::Instance().Has<service::IAppInfoService>());
    MessageHandler handler;
    std::vector<service::PoolStatusDto> pools;
    const bool populated = GENERATE(false, true);
    if (populated) {
        pools = {{128, 3, 2, {{11, 23, 99}, {12, 24, 100}}}, {512, 1, 1, {{13, 25, 101}}}};
    }
    REQUIRE_CALL(app_info, GetMemoryPoolStatus()).RETURN(pools);
    std::error_condition errc = util::ErrorEnum::Success;
    auto response             = handler.Handle(MsgQueryDeviceMemStatusRecv{}, errc);
    REQUIRE(errc == util::ErrorEnum::Success);
    REQUIRE(response.status.size() == pools.size());
    REQUIRE(response.totalMalloc == (populated ? 1664 : 0));
    REQUIRE(response.totalInUsing == (populated ? 768 : 0));
    for (size_t i = 0; i < pools.size(); ++i) {
        const auto& actual = response.status[i];
        REQUIRE(actual.poolSize == pools[i].pool_size);
        REQUIRE(actual.mallocCnt == pools[i].used_cnt);
        REQUIRE(actual.freeCnt == pools[i].idle_cnt);
        REQUIRE(actual.mallocPoolStatus.size() == pools[i].used_nodes_status.size());
        for (size_t j = 0; j < actual.mallocPoolStatus.size(); ++j) {
            REQUIRE(actual.mallocPoolStatus[j].threadId == pools[i].used_nodes_status[j].thread_id);
            REQUIRE(actual.mallocPoolStatus[j].duration == pools[i].used_nodes_status[j].duration);
            REQUIRE(actual.mallocPoolStatus[j].backtrace.empty());
        }
    }
}
