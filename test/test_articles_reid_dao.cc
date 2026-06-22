#include "catch_amalgamated.hpp"
/// @file test_articles_reid_dao.cc
/// @brief ArticlesReidDao direct unit tests — validates things lib and article CRUD,
///        feature storage/update/clear at the DAO layer using in-memory SQLite.

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/ArticlesReidDao.h"

namespace cosmo::test {

namespace {

    SQLite::Database MakeTestDb() {
        SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::ArticlesReidDao dao(db);
        dao.CreateTable();
        return db;
    }

    db::LibInfo MakeLib(const std::string& id, const std::string& name = "Things Lib", int type = 1,
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

TEST_CASE("ArticlesReidDao: CreateTable is idempotent", "[articles-reid-dao]") {
    auto db = MakeTestDb();
    db::ArticlesReidDao dao(db);
    REQUIRE_NOTHROW(dao.CreateTable());
}

TEST_CASE("ArticlesReidDao: things lib CRUD", "[articles-reid-dao]") {
    auto db = MakeTestDb();
    db::ArticlesReidDao dao(db);

    SECTION("AddArticlesReidLib and QueryThingsLib roundtrip") {
        REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-1", "Bag Lib")));

        db::ThingsLibQueryCondition cond;
        auto result = dao.QueryThingsLib(cond);
        REQUIRE(result.things_lib_count == 1);
        REQUIRE(result.things_lib_list[0].id == "tlib-1");
        REQUIRE(result.things_lib_list[0].name == "Bag Lib");
    }

    SECTION("UpdateArticlesReidLib modifies name") {
        REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-1")));

        auto updated = MakeLib("tlib-1", "Updated Things Lib", 1, 0.8, 300);
        REQUIRE(dao.UpdateArticlesReidLib(updated));

        db::ThingsLibQueryCondition cond;
        auto result = dao.QueryThingsLib(cond);
        REQUIRE(result.things_lib_list[0].name == "Updated Things Lib");
    }

    SECTION("RemoveArticlesReidLib deletes lib and its items") {
        REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-1")));
        REQUIRE(dao.AddArticlesReid("art-1", "tlib-1", "Bag A", {1.0f, 2.0f}));
        REQUIRE(dao.RemoveArticlesReidLib("tlib-1"));

        db::ThingsLibQueryCondition cond;
        auto result = dao.QueryThingsLib(cond);
        REQUIRE(result.things_lib_count == 0);

        db::ThingsQueryCondition tcond;
        tcond.things_lib_id_list = {"tlib-1"};
        auto tresult             = dao.QueryThings(tcond);
        REQUIRE(tresult.total_count == 0);
    }

    SECTION("ClearArticlesReidLib removes items but keeps lib") {
        REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-1")));
        REQUIRE(dao.AddArticlesReid("art-1", "tlib-1", "Item", {3.0f}));
        REQUIRE(dao.ClearArticlesReidLib("tlib-1"));

        db::ThingsLibQueryCondition cond;
        auto result = dao.QueryThingsLib(cond);
        REQUIRE(result.things_lib_count == 1);

        db::ThingsQueryCondition tcond;
        tcond.things_lib_id_list = {"tlib-1"};
        auto tresult             = dao.QueryThings(tcond);
        REQUIRE(tresult.total_count == 0);
    }

    SECTION("GetAllArticlesReidLibs returns all lib IDs") {
        REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-1")));
        REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-2")));

        auto all = dao.GetAllArticlesReidLibs();
        REQUIRE(all.size() == 2);
    }
}

TEST_CASE("ArticlesReidDao: article CRUD", "[articles-reid-dao]") {
    auto db = MakeTestDb();
    db::ArticlesReidDao dao(db);

    REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-1")));

    SECTION("AddArticlesReid and QueryThings roundtrip with feature") {
        std::vector<float> feature = {10.0f, 20.0f, 30.0f};
        REQUIRE(dao.AddArticlesReid("art-010", "tlib-1", "Backpack", feature));

        db::ThingsQueryCondition cond;
        cond.things_lib_id_list = {"tlib-1"};
        auto result             = dao.QueryThings(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.things_list[0].id == "art-010");
        REQUIRE(result.things_list[0].picture_name == "Backpack");
        REQUIRE(result.things_list[0].feature.size() == 3);
        REQUIRE(result.things_list[0].feature[0] == Catch::Approx(10.0f));
    }

    SECTION("QueryArticlesReidFeature returns stored feature") {
        std::vector<float> feature = {100.0f, 200.0f};
        REQUIRE(dao.AddArticlesReid("art-feat", "tlib-1", "Item", feature));

        auto retrieved = dao.QueryArticlesReidFeature("art-feat");
        REQUIRE(retrieved.size() == 2);
        REQUIRE(retrieved[0] == Catch::Approx(100.0f));
        REQUIRE(retrieved[1] == Catch::Approx(200.0f));
    }

    SECTION("QueryArticlesReidFeature for non-existent returns empty") {
        auto retrieved = dao.QueryArticlesReidFeature("nonexistent");
        REQUIRE(retrieved.empty());
    }

    SECTION("UpdateArticlesReid modifies name") {
        REQUIRE(dao.AddArticlesReid("art-011", "tlib-1", "Old Name", {5.0f}));
        REQUIRE(dao.UpdateArticlesReid("art-011", "New Name"));

        db::ThingsQueryCondition cond;
        cond.things_id = "art-011";
        auto result    = dao.QueryThings(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.things_list[0].picture_name == "New Name");
    }

    SECTION("UpdateFeature modifies stored feature") {
        REQUIRE(dao.AddArticlesReid("art-012", "tlib-1", "Item", {1.0f, 2.0f}));

        std::vector<float> newFeature = {99.0f, 88.0f, 77.0f};
        REQUIRE(dao.UpdateFeature("art-012", newFeature));

        auto retrieved = dao.QueryArticlesReidFeature("art-012");
        REQUIRE(retrieved.size() == 3);
        REQUIRE(retrieved[0] == Catch::Approx(99.0f));
    }

    SECTION("UpdateFeature for non-existent returns false") {
        REQUIRE_FALSE(dao.UpdateFeature("nonexistent", {1.0f}));
    }

    SECTION("RemoveArticlesReid deletes article") {
        REQUIRE(dao.AddArticlesReid("art-013", "tlib-1", "Item", {6.0f}));
        REQUIRE(dao.RemoveArticlesReid("art-013"));

        db::ThingsQueryCondition cond;
        cond.things_id = "art-013";
        auto result    = dao.QueryThings(cond);
        REQUIRE(result.total_count == 0);
    }

    SECTION("QueryThings by picture_name (LIKE search)") {
        REQUIRE(dao.AddArticlesReid("art-020", "tlib-1", "Backpack", {7.0f}));
        REQUIRE(dao.AddArticlesReid("art-021", "tlib-1", "Briefcase", {8.0f}));

        db::ThingsQueryCondition cond;
        cond.picture_name = "Back";
        auto result       = dao.QueryThings(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.things_list[0].id == "art-020");
    }
}

TEST_CASE("ArticlesReidDao: ClearFeature removes all features", "[articles-reid-dao]") {
    auto db = MakeTestDb();
    db::ArticlesReidDao dao(db);

    REQUIRE(dao.AddArticlesReidLib(MakeLib("tlib-1")));
    REQUIRE(dao.AddArticlesReid("art-030", "tlib-1", "Item A", {1.0f, 2.0f, 3.0f}));
    REQUIRE(dao.AddArticlesReid("art-031", "tlib-1", "Item B", {4.0f, 5.0f}));

    // Verify features exist before clear
    REQUIRE_FALSE(dao.QueryArticlesReidFeature("art-030").empty());
    REQUIRE_FALSE(dao.QueryArticlesReidFeature("art-031").empty());

    dao.ClearFeature();

    // Features should be cleared
    REQUIRE(dao.QueryArticlesReidFeature("art-030").empty());
    REQUIRE(dao.QueryArticlesReidFeature("art-031").empty());

    // Items should still exist (just without features)
    db::ThingsQueryCondition cond;
    cond.things_lib_id_list = {"tlib-1"};
    auto result             = dao.QueryThings(cond);
    REQUIRE(result.total_count == 2);
}

}  // namespace cosmo::test
