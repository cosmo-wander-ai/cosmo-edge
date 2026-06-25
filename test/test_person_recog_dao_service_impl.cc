#include "catch_amalgamated.hpp"
/// @file test_person_recog_dao_service_impl.cc
/// @brief PersonRecogDaoServiceImpl unit tests — validates full CRUD cycle
///        using an in-memory SQLite database.

#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>

#include "db/PersonRecogDao.h"
#include "mock/MockDbService.h"
#include "mock/MockServiceRegistry.h"
#include "service/face/impl/PersonRecogDaoServiceImpl.h"

namespace cosmo::test {

namespace {

    /// Creates an in-memory SQLite database with PersonRecogDao tables initialized.
    std::shared_ptr<SQLite::Database> MakeTestDb() {
        auto db =
            std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::PersonRecogDao dao(*db);
        dao.CreateTable();
        return db;
    }

}  // namespace

TEST_CASE("PersonRecogDaoService: person lib CRUD cycle", "[person-recog-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::PersonRecogDaoServiceImpl sut;

    db::LibInfo lib;
    lib.id           = "plib-001";
    lib.name         = "Body Lib A";
    lib.type         = 1;
    lib.threshold    = 0.7;
    lib.max_capacity = 200;

    SECTION("AddPersonLib succeeds") {
        REQUIRE(sut.AddPersonLib(lib));
    }

    SECTION("QueryPersonLib returns inserted lib") {
        REQUIRE(sut.AddPersonLib(lib));

        db::PersonRecogLibQueryCondition cond;
        auto result = sut.QueryPersonLib(cond);
        REQUIRE(result.person_lib_count == 1);
        REQUIRE(result.person_lib_list.size() == 1);
        REQUIRE(result.person_lib_list[0].id == "plib-001");
        REQUIRE(result.person_lib_list[0].name == "Body Lib A");
        REQUIRE(result.person_lib_list[0].type == 1);
    }

    SECTION("UpdatePersonLib modifies name") {
        REQUIRE(sut.AddPersonLib(lib));

        db::LibInfo updated = lib;
        updated.name        = "Body Lib A Updated";
        REQUIRE(sut.UpdatePersonLib(updated));

        db::PersonRecogLibQueryCondition cond;
        auto result = sut.QueryPersonLib(cond);
        REQUIRE(result.person_lib_list.size() == 1);
        REQUIRE(result.person_lib_list[0].name == "Body Lib A Updated");
    }

    SECTION("RemovePersonLib deletes lib and its persons") {
        REQUIRE(sut.AddPersonLib(lib));
        REQUIRE(sut.AddPerson("p-001", "plib-001", "Alice", {1.0f, 2.0f, 3.0f}));
        REQUIRE(sut.RemovePersonLib("plib-001"));

        db::PersonRecogLibQueryCondition cond;
        auto result = sut.QueryPersonLib(cond);
        REQUIRE(result.person_lib_count == 0);

        // Persons should also be gone
        db::PersonRecogQueryCondition pcond;
        pcond.person_lib_id_list = {"plib-001"};
        auto presult             = sut.QueryPersons(pcond);
        REQUIRE(presult.total_count == 0);
    }

    SECTION("ClearPersonLib removes persons but keeps lib") {
        REQUIRE(sut.AddPersonLib(lib));
        REQUIRE(sut.AddPerson("p-002", "plib-001", "Bob", {4.0f, 5.0f}));
        REQUIRE(sut.ClearPersonLib("plib-001"));

        // Lib still exists
        db::PersonRecogLibQueryCondition cond;
        auto result = sut.QueryPersonLib(cond);
        REQUIRE(result.person_lib_count == 1);

        // Persons are gone
        db::PersonRecogQueryCondition pcond;
        pcond.person_lib_id_list = {"plib-001"};
        auto presult             = sut.QueryPersons(pcond);
        REQUIRE(presult.total_count == 0);
    }

    SECTION("GetAllPersonLibs returns all lib IDs") {
        REQUIRE(sut.AddPersonLib(lib));

        db::LibInfo lib2 = lib;
        lib2.id          = "plib-002";
        lib2.name        = "Body Lib B";
        REQUIRE(sut.AddPersonLib(lib2));

        auto allLibs = sut.GetAllPersonLibs();
        REQUIRE(allLibs.size() == 2);
    }
}

TEST_CASE("PersonRecogDaoService: person CRUD cycle", "[person-recog-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::PersonRecogDaoServiceImpl sut;

    // Pre-create a person library
    db::LibInfo lib;
    lib.id           = "plib-001";
    lib.name         = "Lib";
    lib.type         = 1;
    lib.threshold    = 0.7;
    lib.max_capacity = 100;
    REQUIRE(sut.AddPersonLib(lib));

    SECTION("AddPerson succeeds and is queryable") {
        std::vector<float> feature = {10.0f, 20.0f, 30.0f};
        REQUIRE(sut.AddPerson("p-010", "plib-001", "Charlie", feature));

        db::PersonRecogQueryCondition cond;
        cond.person_lib_id_list = {"plib-001"};
        auto result             = sut.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].id == "p-010");
        REQUIRE(result.person_list[0].picture_name == "Charlie");
        // Verify feature vector roundtrip
        REQUIRE(result.person_list[0].feature.size() == 3);
        REQUIRE(result.person_list[0].feature[0] == Catch::Approx(10.0f));
        REQUIRE(result.person_list[0].feature[2] == Catch::Approx(30.0f));
    }

    SECTION("RemovePerson deletes person") {
        REQUIRE(sut.AddPerson("p-011", "plib-001", "Dave", {5.0f, 6.0f}));
        REQUIRE(sut.RemovePerson("p-011"));

        db::PersonRecogQueryCondition cond;
        cond.person_id = "p-011";
        auto result    = sut.QueryPersons(cond);
        REQUIRE(result.total_count == 0);
    }

    SECTION("Multiple persons in same lib") {
        REQUIRE(sut.AddPerson("p-020", "plib-001", "Eve", {1.0f}));
        REQUIRE(sut.AddPerson("p-021", "plib-001", "Frank", {2.0f}));

        db::PersonRecogQueryCondition cond;
        cond.person_lib_id_list = {"plib-001"};
        auto result             = sut.QueryPersons(cond);
        REQUIRE(result.total_count == 2);
    }
}

TEST_CASE("PersonRecogDaoService: transaction control", "[person-recog-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::PersonRecogDaoServiceImpl sut;

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
