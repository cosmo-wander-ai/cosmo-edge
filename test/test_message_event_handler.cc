// Unit tests for MessageEventHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageEventHandler.h"
#include "mock/MockAlarmRecordService.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockNetworkConfig.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

struct EventHandlerMocks {
    MockAlarmRecordService alarmRecordSvc;
    MockAlgorithmService algSvc;
    MockNetworkConfig networkSvc;
};

MessageEventHandler MakeHandler(EventHandlerMocks& mocks) {
    return MessageEventHandler(mocks.alarmRecordSvc, mocks.algSvc, mocks.networkSvc);
}

}  // namespace

TEST_CASE("EventHandler: QueryAlarmEvent with valid pagination", "[event-handler]") {
    EventHandlerMocks mocks;
    ALLOW_CALL(mocks.algSvc, GetAlgorithmName(trompeloeil::_)).RETURN("");
    auto handler = MakeHandler(mocks);

    std::vector<cosmo::MsgEventUnit> events(2);
    REQUIRE_CALL(mocks.alarmRecordSvc, QueryEvents(trompeloeil::_, trompeloeil::_))
        .LR_SIDE_EFFECT(_2 = 2)
        .RETURN(events);

    Event::MsgPageRecv data{};
    data.pageNum  = 1;
    data.pageSize = 10;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.total == 2);
}

TEST_CASE("EventHandler: QueryAlarmEvent empty result", "[event-handler]") {
    EventHandlerMocks mocks;
    ALLOW_CALL(mocks.algSvc, GetAlgorithmName(trompeloeil::_)).RETURN("");
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.alarmRecordSvc, QueryEvents(trompeloeil::_, trompeloeil::_))
        .LR_SIDE_EFFECT(_2 = 0)
        .RETURN(std::vector<cosmo::MsgEventUnit>{});

    Event::MsgPageRecv data{};
    data.pageNum  = 1;
    data.pageSize = 10;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.total == 0);
    REQUIRE(ret.resData.rows.empty());
}

TEST_CASE("EventHandler: QueryPassengerFlow", "[event-handler]") {
    EventHandlerMocks mocks;
    ALLOW_CALL(mocks.algSvc, GetAlgorithmName(trompeloeil::_)).RETURN("");
    auto handler = MakeHandler(mocks);

    cosmo::service::FlowQueryResult flowResult;
    flowResult.totalCount = 5;
    REQUIRE_CALL(mocks.alarmRecordSvc, QueryPassengerFlow(_)).RETURN(flowResult);

    Event::MsgQueryPassengerFlowNumberRecv data{};
    data.channelId     = "cam-1";
    data.algorithmCode = "person_count";
    data.type          = 1;  // Hourly
    data.startTime     = "2023-01-01 00:00:00";
    data.endTime       = "2023-01-01 05:00:00";  // 5 hours gap
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.totalCount == 5);
}

TEST_CASE("Attribute queries validate scene scope and return requested full summaries",
          "[attributes][event-handler]") {
    EventHandlerMocks mocks;
    auto handler = MakeHandler(mocks);
    Event::MsgPageRecv query;
    query.categorys               = {"12"};
    query.algorithmCodes          = {"scene"};
    query.timeBegin               = 1000;
    query.timeEnd                 = 6000;
    query.pageNum                 = 1;
    query.pageSize                = 20;
    query.includeAttributeSummary = true;
    SECTION("valid query") {
        REQUIRE_CALL(mocks.alarmRecordSvc, QueryEvents(_, _))
            .LR_SIDE_EFFECT(_2 = 0)
            .RETURN(std::vector<MsgEventUnit>{});
        REQUIRE_CALL(mocks.alarmRecordSvc, QueryAttributeSummary(_)).RETURN(AttributeSummary{});
        std::error_condition error;
        const auto result = handler.Handle(std::move(query), error);
        REQUIRE_FALSE(error);
        REQUIRE(result.resData.attributeSummary.schemas.empty());
        return;
    }
    SECTION("multiple scenes") {
        query.algorithmCodes.push_back("other");
    }
    SECTION("missing time range") {
        query.timeEnd = 0;
    }
    SECTION("filter without schema") {
        query.attributeFilters = {{"hat", "yes", "valid"}};
    }
    SECTION("bad status") {
        query.attributeSchemaId = "schema";
        query.attributeFilters  = {{"hat", "", "absent"}};
    }
    SECTION("excess channels") {
        query.channelIds.resize(129, "channel");
    }
    std::error_condition error;
    const auto result = handler.Handle(std::move(query), error);
    REQUIRE(error == cosmo::util::ErrorEnum::InvalidParam);
    REQUIRE(result.resData.attributeSummary.schemas.empty());
}
