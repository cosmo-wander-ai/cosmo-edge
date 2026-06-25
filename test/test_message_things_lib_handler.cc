// Unit tests for MessageThingsLibHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageThingsLibHandler.h"
#include "mock/MockArticlesReidDaoService.h"
#include "mock/MockCameraService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockVideoFrameCodec.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

MessageThingsLibHandler MakeHandler(MockServiceRegistry& mocks) {
    return MessageThingsLibHandler(mocks.articlesReidDaoSvc,
                                   static_cast<cosmo::service::ICameraTaskConfig&>(mocks.cameraSvc),
                                   mocks.videoCodecSvc);
}

}  // namespace

TEST_CASE("ThingsLibHandler: ModifyThingsLib Add", "[things-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    SECTION("Normal add creates lib") {
        REQUIRE_CALL(mocks.articlesReidDaoSvc, Begin());
        REQUIRE_CALL(mocks.articlesReidDaoSvc, AddArticlesReidLib(_)).RETURN(true);
        REQUIRE_CALL(mocks.articlesReidDaoSvc, Commit());

        ThingsLib::MsgModifyThingsLibRecv data{};
        data.thingsLibOperation = 1;  // Add
        data.thingsLib.name     = "test-things-lib";
        data.thingsLib.type     = 1;
        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(!ret.resData.thingsLibId.empty());
    }

    SECTION("Empty name throws") {
        ThingsLib::MsgModifyThingsLibRecv data{};
        data.thingsLibOperation = 1;
        data.thingsLib.name     = "";
        std::error_condition errc;
        REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
    }
}

TEST_CASE("ThingsLibHandler: ModifyThingsLib Update", "[things-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.articlesReidDaoSvc, Begin());
    REQUIRE_CALL(mocks.articlesReidDaoSvc, UpdateArticlesReidLib(_)).RETURN(true);
    REQUIRE_CALL(mocks.articlesReidDaoSvc, Commit());

    ThingsLib::MsgModifyThingsLibRecv data{};
    data.thingsLibOperation = 2;  // Update
    data.thingsLib.id       = "existing-id";
    data.thingsLib.name     = "updated-lib";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.thingsLibId == "existing-id");
}

TEST_CASE("ThingsLibHandler: DeleteThingsLib", "[things-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.articlesReidDaoSvc, RemoveArticlesReidLib("lib-1")).RETURN(true);

    ThingsLib::MsgDeleteThingsLibRecv data{};
    data.thingsLibIdList.emplace_back(std::string("lib-1"));
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.failedThingsLibList.empty());
}

TEST_CASE("ThingsLibHandler: QueryThingsLibInfo", "[things-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    db::ThingsLibQueryResult dbResult{};
    dbResult.search_all       = 1;
    dbResult.things_lib_count = 1;
    db::ThingsLibRecord rec{};
    rec.id                = "lib-1";
    rec.name              = "TestThingsLib";
    rec.max_things_number = 200;
    dbResult.things_lib_list.push_back(rec);
    REQUIRE_CALL(mocks.articlesReidDaoSvc, QueryThingsLib(_)).RETURN(dbResult);

    ThingsLib::MsgQueryThingsLibInfoRecv data{};
    data.pageNum  = 1;
    data.pageSize = 10;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.thingsLibCount == 1);
    REQUIRE(ret.resData.thingsLibList.size() == 1);
}
