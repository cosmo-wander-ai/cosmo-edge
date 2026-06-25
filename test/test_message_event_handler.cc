// Unit tests for MessageEventHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageEventHandler.h"
#include "mock/MockAlarmRecordService.h"
#include "mock/MockAlgorithmService.h"
#include "mock/MockNetworkService.h"
#include "mock/MockServiceRegistry.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

MessageEventHandler MakeHandler(MockServiceRegistry& mocks) {
    return MessageEventHandler(mocks.alarmRecordSvc, mocks.algSvc, mocks.networkSvc);
}

}  // namespace

TEST_CASE("EventHandler: QueryAlarmEvent with valid pagination", "[event-handler]") {
    MockServiceRegistry mocks;
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
    MockServiceRegistry mocks;
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
    MockServiceRegistry mocks;
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
