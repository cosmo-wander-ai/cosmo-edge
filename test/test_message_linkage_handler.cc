#include "catch_amalgamated.hpp"
/*
 * test_message_linkage_handler.cc - MessageLinkageHandler unit tests
 *
 * Tests for linkage strategy CRUD, storage query, and switch operations
 * through the handler layer with DI-injected ILinkageService.
 */
#include "api/MessageLinkageHandler.h"
#include "mock/MockLinkageService.h"
#include "mock/MockServiceRegistry.h"
#include "util/ErrorCode.h"

using namespace cosmo;

TEST_CASE("MessageLinkageHandler: Add strategy", "[LinkageHandler]") {
    test::MockServiceRegistry mocks;
    MessageLinkageHandler handler(mocks.linkageSvc);

    SECTION("Successful add returns generated id") {
        ALLOW_CALL(mocks.linkageSvc, Add(trompeloeil::_, trompeloeil::_, trompeloeil::_))
            .SIDE_EFFECT(_3 = "linkage-uuid-001")
            .RETURN(util::ErrorEnum::Success);

        Linkage::MsgAddRecv req{};
        req.name     = "Test Strategy";
        req.workFlow = "[{\"type\":\"audio\"}]";
        std::error_condition errc;
        auto rsp = handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Success);
        REQUIRE(rsp.resData.id == "linkage-uuid-001");
    }

    SECTION("Add failure propagates error") {
        ALLOW_CALL(mocks.linkageSvc, Add(trompeloeil::_, trompeloeil::_, trompeloeil::_))
            .RETURN(util::ErrorEnum::Failed);

        Linkage::MsgAddRecv req{};
        req.name     = "Bad Strategy";
        req.workFlow = "invalid";
        std::error_condition errc;
        auto rsp = handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Failed);
    }
}

TEST_CASE("MessageLinkageHandler: Delete strategy", "[LinkageHandler]") {
    test::MockServiceRegistry mocks;
    MessageLinkageHandler handler(mocks.linkageSvc);

    SECTION("Successful delete") {
        ALLOW_CALL(mocks.linkageSvc, Delete(trompeloeil::_)).RETURN(util::ErrorEnum::Success);

        Linkage::MsgDeleteRecv req{};
        req.id = "linkage-001";
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Success);
    }

    SECTION("Delete non-existent ID returns error") {
        ALLOW_CALL(mocks.linkageSvc, Delete(trompeloeil::_)).RETURN(util::ErrorEnum::IDNotExist);

        Linkage::MsgDeleteRecv req{};
        req.id = "non-existent";
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::IDNotExist);
    }
}

TEST_CASE("MessageLinkageHandler: Update strategy", "[LinkageHandler]") {
    test::MockServiceRegistry mocks;
    MessageLinkageHandler handler(mocks.linkageSvc);

    SECTION("Successful update") {
        ALLOW_CALL(mocks.linkageSvc, Update(trompeloeil::_, trompeloeil::_, trompeloeil::_))
            .RETURN(util::ErrorEnum::Success);

        Linkage::MsgUpdateRecv req{};
        req.name     = "Updated Strategy";
        req.id       = "linkage-001";
        req.workFlow = "[{\"type\":\"light\"}]";
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Success);
    }

    SECTION("Update non-existent strategy returns error") {
        ALLOW_CALL(mocks.linkageSvc, Update(trompeloeil::_, trompeloeil::_, trompeloeil::_))
            .RETURN(util::ErrorEnum::StrategyNotExist);

        Linkage::MsgUpdateRecv req{};
        req.id = "non-existent";
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::StrategyNotExist);
    }
}

TEST_CASE("MessageLinkageHandler: Page query", "[LinkageHandler]") {
    test::MockServiceRegistry mocks;
    MessageLinkageHandler handler(mocks.linkageSvc);

    std::vector<LinkageStrategyOutputUnit> mockResults;
    LinkageStrategyOutputUnit u1;
    u1.id     = "linkage-001";
    u1.name   = "Strategy A";
    u1.status = true;
    mockResults.push_back(u1);

    ALLOW_CALL(mocks.linkageSvc, Query(trompeloeil::_, trompeloeil::_, trompeloeil::_, trompeloeil::_))
        .SIDE_EFFECT(_4 = 1)
        .RETURN(mockResults);

    Linkage::MsgPageRecv req{};
    req.pageNum  = 1;
    req.pageSize = 10;
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.resData.tasks.size() == 1);
    REQUIRE(rsp.resData.total == 1);
    REQUIRE(rsp.resData.tasks[0].id == "linkage-001");
}

TEST_CASE("MessageLinkageHandler: Storages query", "[LinkageHandler]") {
    test::MockServiceRegistry mocks;
    MessageLinkageHandler handler(mocks.linkageSvc);

    ALLOW_CALL(mocks.linkageSvc, ReadSupportedStorage(trompeloeil::_, trompeloeil::_))
        .SIDE_EFFECT({
            _1 = 2;
            StorageList s1;
            s1.id         = "storage-001";
            s1.actionName = "Audio Play";
            StorageList s2;
            s2.id         = "storage-002";
            s2.actionName = "Light Flash";
            _2            = {s1, s2};
        })
        .RETURN(true);

    Linkage::MsgStoragesRecv req{};
    std::error_condition errc;
    auto rsp = handler.Handle(std::move(req), errc);

    REQUIRE(rsp.resData.storages.size() == 2);
    REQUIRE(rsp.resData.storages[0].id == "storage-001");
}

TEST_CASE("MessageLinkageHandler: Switch strategy", "[LinkageHandler]") {
    test::MockServiceRegistry mocks;
    MessageLinkageHandler handler(mocks.linkageSvc);

    SECTION("Enable strategy") {
        ALLOW_CALL(mocks.linkageSvc, Switch(trompeloeil::_, true)).RETURN(util::ErrorEnum::Success);

        Linkage::MsgSwitchRecv req{};
        req.id     = "linkage-001";
        req.enable = true;
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Success);
    }

    SECTION("Disable strategy") {
        ALLOW_CALL(mocks.linkageSvc, Switch(trompeloeil::_, false)).RETURN(util::ErrorEnum::Success);

        Linkage::MsgSwitchRecv req{};
        req.id     = "linkage-001";
        req.enable = false;
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::Success);
    }

    SECTION("Switch non-existent strategy returns error") {
        ALLOW_CALL(mocks.linkageSvc, Switch(trompeloeil::_, trompeloeil::_))
            .RETURN(util::ErrorEnum::IDNotExist);

        Linkage::MsgSwitchRecv req{};
        req.id     = "non-existent";
        req.enable = true;
        std::error_condition errc;
        (void)handler.Handle(std::move(req), errc);

        REQUIRE(errc == util::ErrorEnum::IDNotExist);
    }
}
