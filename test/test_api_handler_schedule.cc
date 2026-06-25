#include "catch_amalgamated.hpp"
/*
 * test_api_handler_schedule.cc - MessageScheduleHandler unit tests
 *
 * Tests for schedule CRUD operations through handler layer.
 */
#include "api/MessageScheduleHandler.h"
#include "mock/MockCameraService.h"
#include "mock/MockScheduleService.h"
#include "mock/MockServiceRegistry.h"
#include "util/ErrorCode.h"

using namespace cosmo;

TEST_CASE("MessageScheduleHandler: Add", "[ScheduleHandler]") {
    test::MockServiceRegistry mocks;
    MessageScheduleHandler handler(mocks.scheduleSvc,
                                   static_cast<service::ICameraTaskConfig&>(mocks.cameraSvc));

    SECTION("Successful add returns new id") {
        ALLOW_CALL(mocks.scheduleSvc, Add(trompeloeil::_, trompeloeil::_))
            .SIDE_EFFECT(_2 = "new-sched-001")
            .RETURN(util::ErrorEnum::Success);

        Schedule::MsgAddRecv req{};
        req.scheduleName = "Morning Schedule";
        std::error_condition errc;
        auto rsp = handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Success);
        REQUIRE(rsp.resData.id == "new-sched-001");
    }

    SECTION("Add failure propagates error") {
        ALLOW_CALL(mocks.scheduleSvc, Add(trompeloeil::_, trompeloeil::_))
            .RETURN(util::ErrorEnum::ParameterException);

        Schedule::MsgAddRecv req{};
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::ParameterException);
    }
}

TEST_CASE("MessageScheduleHandler: Update", "[ScheduleHandler]") {
    test::MockServiceRegistry mocks;
    MessageScheduleHandler handler(mocks.scheduleSvc,
                                   static_cast<service::ICameraTaskConfig&>(mocks.cameraSvc));

    ALLOW_CALL(mocks.scheduleSvc, Update(trompeloeil::_)).RETURN(util::ErrorEnum::Success);

    Schedule::MsgUpdateRecv req{};
    req.scheduleId   = "sched-001";
    req.scheduleName = "Updated Schedule";
    std::error_condition errc;
    (void)handler.Handle(std::move(req), errc);

    REQUIRE(errc == util::ErrorEnum::Success);
}

TEST_CASE("MessageScheduleHandler: Page query", "[ScheduleHandler]") {
    test::MockServiceRegistry mocks;
    MessageScheduleHandler handler(mocks.scheduleSvc,
                                   static_cast<service::ICameraTaskConfig&>(mocks.cameraSvc));

    std::vector<MsgScheduleTemplate> mockResults;
    MsgScheduleTemplate t1;
    t1.scheduleId   = "sched-001";
    t1.scheduleName = "Template A";
    mockResults.push_back(t1);

    ALLOW_CALL(mocks.scheduleSvc, Query(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .SIDE_EFFECT(_4 = 1)
        .RETURN(mockResults);

    Schedule::MsgPageRecv req{};
    req.pageNum  = 1;
    req.pageSize = 10;
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.resData.rows.size() == 1);
    REQUIRE(rsp.resData.total == 1);
}

TEST_CASE("MessageScheduleHandler: Delete in-use schedule", "[ScheduleHandler]") {
    test::MockServiceRegistry mocks;
    MessageScheduleHandler handler(mocks.scheduleSvc,
                                   static_cast<service::ICameraTaskConfig&>(mocks.cameraSvc));

    SECTION("In-use schedule returns error") {
        ALLOW_CALL(mocks.cameraSvc, ScheduleInUse("sched-in-use")).RETURN(true);

        Schedule::MsgDeleteRecv req{};
        req.scheduleId = "sched-in-use";
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::TimeTemplateInUse);
    }

    SECTION("Unused schedule deletes successfully") {
        ALLOW_CALL(mocks.cameraSvc, ScheduleInUse("sched-free")).RETURN(false);
        ALLOW_CALL(mocks.scheduleSvc, Delete("sched-free")).RETURN(util::ErrorEnum::Success);

        Schedule::MsgDeleteRecv req{};
        req.scheduleId = "sched-free";
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Success);
    }
}

TEST_CASE("MessageScheduleHandler: SelectScheduleInfo returns all", "[ScheduleHandler]") {
    test::MockServiceRegistry mocks;
    MessageScheduleHandler handler(mocks.scheduleSvc,
                                   static_cast<service::ICameraTaskConfig&>(mocks.cameraSvc));

    std::vector<MsgScheduleTemplate> all;
    MsgScheduleTemplate t;
    t.scheduleId = "sched-all";
    all.push_back(t);

    ALLOW_CALL(mocks.scheduleSvc, Query(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .SIDE_EFFECT(_4 = 1)
        .RETURN(all);

    Schedule::MsgSelectScheduleInfoRecv req{};
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.resData.rows.size() == 1);
}
