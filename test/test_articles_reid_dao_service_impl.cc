#include "catch_amalgamated.hpp"
/// @file test_articles_reid_dao_service_impl.cc
/// @brief ArticlesReidDaoServiceImpl unit tests — validates full CRUD cycle
///        using an in-memory SQLite database.

#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>

#include "db/ArticlesReidDao.h"
#include "mock/MockDbService.h"
#include "mock/MockServiceRegistry.h"
#include "service/face/impl/ArticlesReidDaoServiceImpl.h"

namespace cosmo::test {

namespace {

    /// Creates an in-memory SQLite database with ArticlesReidDao tables initialized.
    std::shared_ptr<SQLite::Database> MakeTestDb() {
        auto db =
            std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::ArticlesReidDao dao(*db);
        dao.CreateTable();
        return db;
    }

}  // namespace

TEST_CASE("ArticlesReidDaoService: things lib CRUD cycle", "[articles-reid-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::ArticlesReidDaoServiceImpl sut;

    db::LibInfo lib;
    lib.id           = "things-lib-001";
    lib.name         = "Work Clothes Lib";
    lib.type         = 1;
    lib.threshold    = 0.75;
    lib.max_capacity = 50;

    SECTION("AddArticlesReidLib succeeds") {
        REQUIRE(sut.AddArticlesReidLib(lib));
    }

    SECTION("QueryThingsLib returns inserted lib") {
        REQUIRE(sut.AddArticlesReidLib(lib));

        db::ThingsLibQueryCondition cond;
        auto result = sut.QueryThingsLib(cond);
        REQUIRE(result.things_lib_count == 1);
        REQUIRE(result.things_lib_list.size() == 1);
        REQUIRE(result.things_lib_list[0].id == "things-lib-001");
        REQUIRE(result.things_lib_list[0].name == "Work Clothes Lib");
        REQUIRE(result.things_lib_list[0].type == 1);
    }

    SECTION("UpdateArticlesReidLib modifies name and type") {
        REQUIRE(sut.AddArticlesReidLib(lib));

        db::LibInfo updated = lib;
        updated.name        = "Updated Clothes Lib";
        updated.type        = 2;
        REQUIRE(sut.UpdateArticlesReidLib(updated));

        db::ThingsLibQueryCondition cond;
        auto result = sut.QueryThingsLib(cond);
        REQUIRE(result.things_lib_list.size() == 1);
        REQUIRE(result.things_lib_list[0].name == "Updated Clothes Lib");
    }

    SECTION("RemoveArticlesReidLib deletes lib and its items") {
        REQUIRE(sut.AddArticlesReidLib(lib));
        REQUIRE(sut.AddArticlesReid("item-001", "things-lib-001", "shirt", {1.0f, 2.0f}));
        REQUIRE(sut.RemoveArticlesReidLib("things-lib-001"));

        db::ThingsLibQueryCondition cond;
        auto result = sut.QueryThingsLib(cond);
        REQUIRE(result.things_lib_count == 0);

        // Items should also be gone
        db::ThingsQueryCondition icond;
        icond.things_lib_id_list = {"things-lib-001"};
        auto iresult             = sut.QueryThings(icond);
        REQUIRE(iresult.total_count == 0);
    }

    SECTION("ClearArticlesReidLib removes items but keeps lib") {
        REQUIRE(sut.AddArticlesReidLib(lib));
        REQUIRE(sut.AddArticlesReid("item-002", "things-lib-001", "pants", {3.0f, 4.0f}));
        REQUIRE(sut.ClearArticlesReidLib("things-lib-001"));

        // Lib still exists
        db::ThingsLibQueryCondition cond;
        auto result = sut.QueryThingsLib(cond);
        REQUIRE(result.things_lib_count == 1);

        // Items are gone
        db::ThingsQueryCondition icond;
        icond.things_lib_id_list = {"things-lib-001"};
        auto iresult             = sut.QueryThings(icond);
        REQUIRE(iresult.total_count == 0);
    }

    SECTION("GetAllArticlesReidLibs returns all lib IDs") {
        REQUIRE(sut.AddArticlesReidLib(lib));

        db::LibInfo lib2 = lib;
        lib2.id          = "things-lib-002";
        lib2.name        = "Second Lib";
        REQUIRE(sut.AddArticlesReidLib(lib2));

        auto allLibs = sut.GetAllArticlesReidLibs();
        REQUIRE(allLibs.size() == 2);
    }
}

TEST_CASE("ArticlesReidDaoService: articles CRUD cycle", "[articles-reid-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::ArticlesReidDaoServiceImpl sut;

    // Pre-create a things library
    db::LibInfo lib;
    lib.id           = "things-lib-001";
    lib.name         = "Lib";
    lib.type         = 1;
    lib.threshold    = 0.75;
    lib.max_capacity = 50;
    REQUIRE(sut.AddArticlesReidLib(lib));

    SECTION("AddArticlesReid succeeds and is queryable") {
        std::vector<float> feature = {1.1f, 2.2f, 3.3f};
        REQUIRE(sut.AddArticlesReid("item-010", "things-lib-001", "helmet", feature));

        db::ThingsQueryCondition cond;
        cond.things_lib_id_list = {"things-lib-001"};
        auto result             = sut.QueryThings(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.things_list[0].id == "item-010");
        REQUIRE(result.things_list[0].picture_name == "helmet");
        // Verify feature vector roundtrip
        REQUIRE(result.things_list[0].feature.size() == 3);
        REQUIRE(result.things_list[0].feature[0] == Catch::Approx(1.1f));
    }

    SECTION("RemoveArticlesReid deletes item") {
        REQUIRE(sut.AddArticlesReid("item-011", "things-lib-001", "vest", {4.0f}));
        REQUIRE(sut.RemoveArticlesReid("item-011"));

        db::ThingsQueryCondition cond;
        cond.things_id = "item-011";
        auto result    = sut.QueryThings(cond);
        REQUIRE(result.total_count == 0);
    }
}

TEST_CASE("ArticlesReidDaoService: transaction control", "[articles-reid-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::ArticlesReidDaoServiceImpl sut;

    SECTION("Begin/Commit does not throw") {
        REQUIRE_NOTHROW(sut.Begin());
        REQUIRE_NOTHROW(sut.Commit());
    }

    SECTION("Begin/Rollback does not throw") {
        REQUIRE_NOTHROW(sut.Begin());
        REQUIRE_NOTHROW(sut.Rollback());
    }
}

}  // namespace cosmo::test
