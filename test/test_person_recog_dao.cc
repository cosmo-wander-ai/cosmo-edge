#include "catch_amalgamated.hpp"
/// @file test_person_recog_dao.cc
/// @brief PersonRecogDao direct unit tests — validates person lib and person CRUD,
///        feature storage/update/clear at the DAO layer using in-memory SQLite.

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/PersonRecogDao.h"

namespace cosmo::test {

namespace {

    SQLite::Database MakeTestDb() {
        SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::PersonRecogDao dao(db);
        dao.CreateTable();
        return db;
    }

    db::LibInfo MakeLib(const std::string& id, const std::string& name = "Body Lib", int type = 1,
                        double threshold = 0.7, int capacity = 200) {
        db::LibInfo lib;
        lib.id           = id;
        lib.name         = name;
        lib.type         = type;
        lib.threshold    = threshold;
        lib.max_capacity = capacity;
        return lib;
    }

}  // namespace

TEST_CASE("PersonRecogDao: CreateTable is idempotent", "[person-recog-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonRecogDao dao(db);
    REQUIRE_NOTHROW(dao.CreateTable());
}

TEST_CASE("PersonRecogDao: person lib CRUD at DAO level", "[person-recog-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonRecogDao dao(db);

    SECTION("AddPersonLib and QueryPersonLib roundtrip") {
        REQUIRE(dao.AddPersonLib(MakeLib("plib-1", "Work Clothes Lib")));

        db::PersonRecogLibQueryCondition cond;
        auto result = dao.QueryPersonLib(cond);
        REQUIRE(result.person_lib_count == 1);
        REQUIRE(result.person_lib_list[0].id == "plib-1");
        REQUIRE(result.person_lib_list[0].name == "Work Clothes Lib");
    }

    SECTION("UpdatePersonLib modifies name") {
        REQUIRE(dao.AddPersonLib(MakeLib("plib-1")));

        db::LibInfo updated = MakeLib("plib-1", "Updated Body Lib", 1, 0.8, 300);
        REQUIRE(dao.UpdatePersonLib(updated));

        db::PersonRecogLibQueryCondition cond;
        auto result = dao.QueryPersonLib(cond);
        REQUIRE(result.person_lib_list[0].name == "Updated Body Lib");
    }

    SECTION("RemovePersonLib deletes lib and its persons") {
        REQUIRE(dao.AddPersonLib(MakeLib("plib-1")));
        REQUIRE(dao.AddPerson("p-001", "plib-1", "Alice", {1.0f, 2.0f}));
        REQUIRE(dao.RemovePersonLib("plib-1"));

        db::PersonRecogLibQueryCondition cond;
        auto result = dao.QueryPersonLib(cond);
        REQUIRE(result.person_lib_count == 0);

        db::PersonRecogQueryCondition pcond;
        pcond.person_lib_id_list = {"plib-1"};
        auto presult             = dao.QueryPersons(pcond);
        REQUIRE(presult.total_count == 0);
    }

    SECTION("ClearPersonLib removes persons but keeps lib") {
        REQUIRE(dao.AddPersonLib(MakeLib("plib-1")));
        REQUIRE(dao.AddPerson("p-001", "plib-1", "Bob", {3.0f}));
        REQUIRE(dao.ClearPersonLib("plib-1"));

        db::PersonRecogLibQueryCondition cond;
        auto result = dao.QueryPersonLib(cond);
        REQUIRE(result.person_lib_count == 1);

        db::PersonRecogQueryCondition pcond;
        pcond.person_lib_id_list = {"plib-1"};
        auto presult             = dao.QueryPersons(pcond);
        REQUIRE(presult.total_count == 0);
    }

    SECTION("GetAllPersonLibs returns all lib IDs") {
        REQUIRE(dao.AddPersonLib(MakeLib("plib-1")));
        REQUIRE(dao.AddPersonLib(MakeLib("plib-2")));

        auto all = dao.GetAllPersonLibs();
        REQUIRE(all.size() == 2);
    }
}

TEST_CASE("PersonRecogDao: person CRUD at DAO level", "[person-recog-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonRecogDao dao(db);

    REQUIRE(dao.AddPersonLib(MakeLib("plib-1")));

    SECTION("AddPerson and QueryPersons roundtrip with feature") {
        std::vector<float> feature = {10.0f, 20.0f, 30.0f};
        REQUIRE(dao.AddPerson("p-010", "plib-1", "Charlie", feature));

        db::PersonRecogQueryCondition cond;
        cond.person_lib_id_list = {"plib-1"};
        auto result             = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].id == "p-010");
        REQUIRE(result.person_list[0].picture_name == "Charlie");
        REQUIRE(result.person_list[0].feature.size() == 3);
        REQUIRE(result.person_list[0].feature[0] == Catch::Approx(10.0f));
    }

    SECTION("QueryPersonFeature returns stored feature") {
        std::vector<float> feature = {100.0f, 200.0f};
        REQUIRE(dao.AddPerson("p-feat", "plib-1", "Feature", feature));

        auto retrieved = dao.QueryPersonFeature("p-feat");
        REQUIRE(retrieved.size() == 2);
        REQUIRE(retrieved[0] == Catch::Approx(100.0f));
        REQUIRE(retrieved[1] == Catch::Approx(200.0f));
    }

    SECTION("QueryPersonFeature for non-existent returns empty") {
        auto retrieved = dao.QueryPersonFeature("nonexistent");
        REQUIRE(retrieved.empty());
    }

    SECTION("UpdatePerson modifies name") {
        REQUIRE(dao.AddPerson("p-011", "plib-1", "Dave", {5.0f}));
        REQUIRE(dao.UpdatePerson("p-011", "David"));

        db::PersonRecogQueryCondition cond;
        cond.person_id = "p-011";
        auto result    = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].picture_name == "David");
    }

    SECTION("UpdateFeature modifies stored feature") {
        REQUIRE(dao.AddPerson("p-012", "plib-1", "Eve", {1.0f, 2.0f}));

        std::vector<float> newFeature = {99.0f, 88.0f, 77.0f};
        REQUIRE(dao.UpdateFeature("p-012", newFeature));

        auto retrieved = dao.QueryPersonFeature("p-012");
        REQUIRE(retrieved.size() == 3);
        REQUIRE(retrieved[0] == Catch::Approx(99.0f));
    }

    SECTION("UpdateFeature for non-existent returns false") {
        REQUIRE_FALSE(dao.UpdateFeature("nonexistent", {1.0f}));
    }

    SECTION("RemovePerson deletes person") {
        REQUIRE(dao.AddPerson("p-013", "plib-1", "Frank", {6.0f}));
        REQUIRE(dao.RemovePerson("p-013"));

        db::PersonRecogQueryCondition cond;
        cond.person_id = "p-013";
        auto result    = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 0);
    }

    SECTION("QueryPersons by picture_name (LIKE search)") {
        REQUIRE(dao.AddPerson("p-020", "plib-1", "Grace", {7.0f}));
        REQUIRE(dao.AddPerson("p-021", "plib-1", "Greg", {8.0f}));

        db::PersonRecogQueryCondition cond;
        cond.picture_name = "Gra";
        auto result       = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].id == "p-020");
    }
}

TEST_CASE("PersonRecogDao: ClearFeature removes all features", "[person-recog-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonRecogDao dao(db);

    REQUIRE(dao.AddPersonLib(MakeLib("plib-1")));
    REQUIRE(dao.AddPerson("p-030", "plib-1", "Helen", {1.0f, 2.0f, 3.0f}));
    REQUIRE(dao.AddPerson("p-031", "plib-1", "Igor", {4.0f, 5.0f}));

    // Verify features exist before clear
    REQUIRE_FALSE(dao.QueryPersonFeature("p-030").empty());
    REQUIRE_FALSE(dao.QueryPersonFeature("p-031").empty());

    dao.ClearFeature();

    // Features should be cleared
    REQUIRE(dao.QueryPersonFeature("p-030").empty());
    REQUIRE(dao.QueryPersonFeature("p-031").empty());

    // Persons should still exist (just without features)
    db::PersonRecogQueryCondition cond;
    cond.person_lib_id_list = {"plib-1"};
    auto result             = dao.QueryPersons(cond);
    REQUIRE(result.total_count == 2);
}

}  // namespace cosmo::test
