#include "catch_amalgamated.hpp"
/// @file test_person_dao.cc
/// @brief PersonDao direct unit tests — validates face lib and person CRUD
///        at the DAO layer using an in-memory SQLite database.

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/PersonDao.h"

namespace cosmo::test {

namespace {

    SQLite::Database MakeTestDb() {
        SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::PersonDao dao(db);
        dao.CreateTable();
        return db;
    }

    db::LibInfo MakeLib(const std::string& id, const std::string& name = "Test Lib", double threshold = 0.8,
                        int capacity = 100) {
        db::LibInfo lib;
        lib.id           = id;
        lib.name         = name;
        lib.threshold    = threshold;
        lib.max_capacity = capacity;
        return lib;
    }

}  // namespace

TEST_CASE("PersonDao: CreateTable is idempotent", "[person-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonDao dao(db);
    REQUIRE_NOTHROW(dao.CreateTable());
}

TEST_CASE("PersonDao: face lib CRUD at DAO level", "[person-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonDao dao(db);

    SECTION("AddFaceLib and QueryFaceLib roundtrip") {
        REQUIRE(dao.AddFaceLib(MakeLib("lib-1", "Front Gate Lib")));

        db::FaceLibQueryCondition cond;
        cond.face_lib_id = "lib-1";
        auto result      = dao.QueryFaceLib(cond);
        REQUIRE(result.face_lib_count == 1);
        REQUIRE(result.face_lib_list[0].id == "lib-1");
        REQUIRE(result.face_lib_list[0].name == "Front Gate Lib");
    }

    SECTION("UpdateFaceLib modifies name and threshold") {
        REQUIRE(dao.AddFaceLib(MakeLib("lib-1")));

        db::LibInfo updated;
        updated.id           = "lib-1";
        updated.name         = "Updated Lib";
        updated.threshold    = 0.9;
        updated.max_capacity = 200;
        REQUIRE(dao.UpdateFaceLib(updated));

        db::FaceLibQueryCondition cond;
        cond.face_lib_id = "lib-1";
        auto result      = dao.QueryFaceLib(cond);
        REQUIRE(result.face_lib_list[0].name == "Updated Lib");
    }

    SECTION("RemoveFaceLib deletes lib") {
        REQUIRE(dao.AddFaceLib(MakeLib("lib-1")));
        REQUIRE(dao.RemoveFaceLib("lib-1"));

        db::FaceLibQueryCondition cond;
        cond.face_lib_id = "lib-1";
        auto result      = dao.QueryFaceLib(cond);
        REQUIRE(result.face_lib_count == 0);
    }

    SECTION("GetAllFaceLibs returns all lib IDs") {
        REQUIRE(dao.AddFaceLib(MakeLib("lib-1")));
        REQUIRE(dao.AddFaceLib(MakeLib("lib-2")));

        auto all = dao.GetAllFaceLibs();
        REQUIRE(all.size() == 2);
    }

    SECTION("QueryFaceLib with no conditions returns all") {
        REQUIRE(dao.AddFaceLib(MakeLib("lib-1")));
        REQUIRE(dao.AddFaceLib(MakeLib("lib-2")));

        db::FaceLibQueryCondition cond;
        auto result = dao.QueryFaceLib(cond);
        REQUIRE(result.face_lib_count == 2);
    }

    SECTION("QueryFaceLib by name (LIKE search)") {
        REQUIRE(dao.AddFaceLib(MakeLib("lib-1", "Front Gate")));
        REQUIRE(dao.AddFaceLib(MakeLib("lib-2", "Back Door")));

        db::FaceLibQueryCondition cond;
        cond.face_lib_name = "Front";
        auto result        = dao.QueryFaceLib(cond);
        REQUIRE(result.face_lib_count == 1);
        REQUIRE(result.face_lib_list[0].id == "lib-1");
    }
}

TEST_CASE("PersonDao: person CRUD at DAO level", "[person-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonDao dao(db);

    // Pre-create face lib
    REQUIRE(dao.AddFaceLib(MakeLib("lib-1")));

    SECTION("AddPerson with PersonCondition and query back") {
        db::PersonCondition person;
        person.person_id                                              = "p-001";
        person.person_name                                            = "Alice";
        person.face_lib_id                                            = {"lib-1"};
        person.serial_number                                          = "SN001";
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-001", {1.0f, 2.0f, 3.0f}}};
        REQUIRE(dao.AddPerson(person, faces));

        db::FacePersonQueryCondition cond;
        cond.face_lib_id_list = {"lib-1"};
        auto result           = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].id == "p-001");
        REQUIRE(result.person_list[0].name == "Alice");
        REQUIRE(result.person_list[0].serial_number == "SN001");
    }

    SECTION("AddPerson with FaceRegRecordUnit") {
        db::FaceRegRecordUnit reg;
        reg.id               = "p-002";
        reg.face_name        = "Bob";
        reg.serial_name      = "SN002";
        reg.face_lib_id      = {"lib-1"};
        reg.face_create_time = 0;
        reg.face_update_time = 0;
        db::FacePicInfo pic;
        pic.id      = "face-002";
        pic.feature = {4.0f, 5.0f};
        reg.face_pic_infos.push_back(pic);
        REQUIRE(dao.AddPerson(reg));

        db::FacePersonQueryCondition cond;
        cond.face_lib_id_list = {"lib-1"};
        auto result           = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].name == "Bob");
    }

    SECTION("QueryFaceFeature returns stored feature") {
        db::PersonCondition person;
        person.person_id                                              = "p-003";
        person.person_name                                            = "Charlie";
        person.face_lib_id                                            = {"lib-1"};
        std::vector<float> expected                                   = {10.0f, 20.0f, 30.0f};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-003", expected}};
        REQUIRE(dao.AddPerson(person, faces));

        auto feature = dao.QueryFaceFeature("face-003");
        REQUIRE(feature.size() == 3);
        REQUIRE(feature[0] == Catch::Approx(10.0f));
        REQUIRE(feature[1] == Catch::Approx(20.0f));
        REQUIRE(feature[2] == Catch::Approx(30.0f));
    }

    SECTION("QueryFaceFeature for non-existent face returns empty") {
        auto feature = dao.QueryFaceFeature("nonexistent");
        REQUIRE(feature.empty());
    }

    SECTION("UpdatePerson modifies name") {
        db::PersonCondition person;
        person.person_id                                              = "p-004";
        person.person_name                                            = "Dave";
        person.face_lib_id                                            = {"lib-1"};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-004", {7.0f, 8.0f}}};
        REQUIRE(dao.AddPerson(person, faces));

        db::PersonCondition updated                                      = person;
        updated.person_name                                              = "David";
        std::vector<std::pair<std::string, std::vector<float>>> newFaces = {{"face-004b", {9.0f, 10.0f}}};
        REQUIRE(dao.UpdatePerson(updated, newFaces));

        db::FacePersonQueryCondition cond;
        cond.person_id        = "p-004";
        cond.face_lib_id_list = {"lib-1"};
        auto result           = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].name == "David");
    }

    SECTION("RemovePerson deletes person and its details") {
        db::PersonCondition person;
        person.person_id                                              = "p-005";
        person.person_name                                            = "Eve";
        person.face_lib_id                                            = {"lib-1"};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-005", {11.0f}}};
        REQUIRE(dao.AddPerson(person, faces));
        REQUIRE(dao.RemovePerson("p-005"));

        db::FacePersonQueryCondition cond;
        cond.person_id        = "p-005";
        cond.face_lib_id_list = {"lib-1"};
        auto result           = dao.QueryPersons(cond);
        REQUIRE(result.total_count == 0);
    }

    SECTION("ClearFaceLib removes persons but keeps lib") {
        db::PersonCondition person;
        person.person_id                                              = "p-006";
        person.person_name                                            = "Frank";
        person.face_lib_id                                            = {"lib-1"};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-006", {12.0f}}};
        REQUIRE(dao.AddPerson(person, faces));
        REQUIRE(dao.ClearFaceLib("lib-1"));

        // Lib should still exist
        db::FaceLibQueryCondition libCond;
        libCond.face_lib_id = "lib-1";
        auto libResult      = dao.QueryFaceLib(libCond);
        REQUIRE(libResult.face_lib_count == 1);

        // Person should be gone
        db::FacePersonQueryCondition personCond;
        personCond.face_lib_id_list = {"lib-1"};
        auto personResult           = dao.QueryPersons(personCond);
        REQUIRE(personResult.total_count == 0);
    }
}

TEST_CASE("PersonDao: RemoveFacesetPersonRelation", "[person-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonDao dao(db);

    REQUIRE(dao.AddFaceLib(MakeLib("lib-A")));
    REQUIRE(dao.AddFaceLib(MakeLib("lib-B")));

    // Add person to both libs
    db::PersonCondition person;
    person.person_id                                              = "p-multi";
    person.person_name                                            = "Multi";
    person.face_lib_id                                            = {"lib-A", "lib-B"};
    std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-multi", {1.0f}}};
    REQUIRE(dao.AddPerson(person, faces));

    // Remove relation from lib-A
    REQUIRE(dao.RemoveFacesetPersonRelation("p-multi", "lib-A"));

    // Person should still be queryable via lib-B
    db::FacePersonQueryCondition cond;
    cond.face_lib_id_list = {"lib-B"};
    auto result           = dao.QueryPersons(cond);
    REQUIRE(result.total_count == 1);

    // Person should not be queryable via lib-A
    cond.face_lib_id_list = {"lib-A"};
    result                = dao.QueryPersons(cond);
    REQUIRE(result.total_count == 0);
}

TEST_CASE("PersonDao: transaction control", "[person-dao-direct]") {
    auto db = MakeTestDb();
    db::PersonDao dao(db);

    SECTION("Begin/Commit does not throw") {
        REQUIRE_NOTHROW(dao.Begin());
        REQUIRE_NOTHROW(dao.Commit());
    }

    SECTION("Begin/Rollback does not throw") {
        REQUIRE_NOTHROW(dao.Begin());
        REQUIRE_NOTHROW(dao.Rollback());
    }
}

}  // namespace cosmo::test
