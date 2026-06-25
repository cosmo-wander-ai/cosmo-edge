#include "catch_amalgamated.hpp"
/// @file test_person_dao_service_impl.cc
/// @brief PersonDaoServiceImpl unit tests — validates full CRUD cycle
///        using an in-memory SQLite database.

#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>

#include "db/PersonDao.h"
#include "mock/MockDbService.h"
#include "mock/MockServiceRegistry.h"
#include "service/face/impl/PersonDaoServiceImpl.h"

namespace cosmo::test {

namespace {

    /// Creates an in-memory SQLite database with PersonDao tables initialized.
    std::shared_ptr<SQLite::Database> MakeTestDb() {
        auto db =
            std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db::PersonDao dao(*db);
        dao.CreateTable();
        return db;
    }

}  // namespace

TEST_CASE("PersonDaoService: face lib CRUD cycle", "[person-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::PersonDaoServiceImpl sut;

    db::LibInfo lib;
    lib.id           = "lib-001";
    lib.name         = "Test Face Lib";
    lib.threshold    = 0.8;
    lib.max_capacity = 100;

    SECTION("AddFaceLib succeeds") {
        REQUIRE(sut.AddFaceLib(lib));
    }

    SECTION("QueryFaceLib returns inserted lib") {
        REQUIRE(sut.AddFaceLib(lib));

        db::FaceLibQueryCondition cond;
        cond.face_lib_id = "lib-001";
        auto result      = sut.QueryFaceLib(cond);
        REQUIRE(result.face_lib_count == 1);
        REQUIRE(result.face_lib_list.size() == 1);
        REQUIRE(result.face_lib_list[0].id == "lib-001");
        REQUIRE(result.face_lib_list[0].name == "Test Face Lib");
    }

    SECTION("UpdateFaceLib modifies name") {
        REQUIRE(sut.AddFaceLib(lib));

        db::LibInfo updated = lib;
        updated.name        = "Updated Name";
        REQUIRE(sut.UpdateFaceLib(updated));

        db::FaceLibQueryCondition cond;
        cond.face_lib_id = "lib-001";
        auto result      = sut.QueryFaceLib(cond);
        REQUIRE(result.face_lib_list.size() == 1);
        REQUIRE(result.face_lib_list[0].name == "Updated Name");
    }

    SECTION("RemoveFaceLib deletes lib") {
        REQUIRE(sut.AddFaceLib(lib));
        REQUIRE(sut.RemoveFaceLib("lib-001"));

        db::FaceLibQueryCondition cond;
        cond.face_lib_id = "lib-001";
        auto result      = sut.QueryFaceLib(cond);
        REQUIRE(result.face_lib_count == 0);
    }

    SECTION("ClearFaceLib removes persons but keeps lib") {
        REQUIRE(sut.AddFaceLib(lib));

        // Add a person to the lib
        db::PersonCondition person;
        person.person_id                                              = "person-001";
        person.person_name                                            = "Alice";
        person.face_lib_id                                            = {"lib-001"};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-001", {1.0f, 2.0f, 3.0f}}};
        REQUIRE(sut.AddPerson(person, faces));

        // Clear the lib
        REQUIRE(sut.ClearFaceLib("lib-001"));

        // Lib still exists
        db::FaceLibQueryCondition cond;
        cond.face_lib_id = "lib-001";
        auto libResult   = sut.QueryFaceLib(cond);
        REQUIRE(libResult.face_lib_count == 1);

        // But persons are gone
        db::FacePersonQueryCondition pcond;
        pcond.face_lib_id_list = {"lib-001"};
        auto personResult      = sut.QueryPersons(pcond);
        REQUIRE(personResult.total_count == 0);
    }
}

TEST_CASE("PersonDaoService: person CRUD cycle", "[person-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::PersonDaoServiceImpl sut;

    // Pre-create a face library
    db::LibInfo lib;
    lib.id           = "lib-001";
    lib.name         = "Lib";
    lib.threshold    = 0.8;
    lib.max_capacity = 100;
    REQUIRE(sut.AddFaceLib(lib));

    SECTION("AddPerson with PersonCondition succeeds") {
        db::PersonCondition person;
        person.person_id                                              = "person-001";
        person.person_name                                            = "Bob";
        person.face_lib_id                                            = {"lib-001"};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-001", {0.1f, 0.2f, 0.3f}}};
        REQUIRE(sut.AddPerson(person, faces));

        // Query back
        db::FacePersonQueryCondition cond;
        cond.face_lib_id_list = {"lib-001"};
        auto result           = sut.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].name == "Bob");
    }

    SECTION("AddPerson with FaceRegRecordUnit succeeds") {
        db::FaceRegRecordUnit faceReg;
        faceReg.id               = "person-002";
        faceReg.face_name        = "Charlie";
        faceReg.serial_name      = "SN-001";
        faceReg.face_lib_id      = {"lib-001"};
        faceReg.face_create_time = 0;
        faceReg.face_update_time = 0;
        db::FacePicInfo pic;
        pic.id      = "face-002";
        pic.feature = {0.4f, 0.5f, 0.6f};
        faceReg.face_pic_infos.push_back(pic);
        REQUIRE(sut.AddPerson(faceReg));
    }

    SECTION("UpdatePerson modifies name") {
        db::PersonCondition person;
        person.person_id                                              = "person-003";
        person.person_name                                            = "Dave";
        person.face_lib_id                                            = {"lib-001"};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-003", {0.7f, 0.8f, 0.9f}}};
        REQUIRE(sut.AddPerson(person, faces));

        db::PersonCondition updated                                      = person;
        updated.person_name                                              = "David";
        std::vector<std::pair<std::string, std::vector<float>>> newFaces = {
            {"face-003b", {1.0f, 1.1f, 1.2f}}};
        REQUIRE(sut.UpdatePerson(updated, newFaces));

        db::FacePersonQueryCondition cond;
        cond.face_lib_id_list = {"lib-001"};
        cond.person_id        = "person-003";
        auto result           = sut.QueryPersons(cond);
        REQUIRE(result.total_count == 1);
        REQUIRE(result.person_list[0].name == "David");
    }

    SECTION("RemovePerson deletes person") {
        db::PersonCondition person;
        person.person_id                                              = "person-004";
        person.person_name                                            = "Eve";
        person.face_lib_id                                            = {"lib-001"};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-004", {1.3f, 1.4f, 1.5f}}};
        REQUIRE(sut.AddPerson(person, faces));
        REQUIRE(sut.RemovePerson("person-004"));

        db::FacePersonQueryCondition cond;
        cond.face_lib_id_list = {"lib-001"};
        cond.person_id        = "person-004";
        auto result           = sut.QueryPersons(cond);
        REQUIRE(result.total_count == 0);
    }

    SECTION("QueryFaceFeature returns stored feature vector") {
        db::PersonCondition person;
        person.person_id                                              = "person-005";
        person.person_name                                            = "Frank";
        person.face_lib_id                                            = {"lib-001"};
        std::vector<float> expectedFeature                            = {10.0f, 20.0f, 30.0f};
        std::vector<std::pair<std::string, std::vector<float>>> faces = {{"face-005", expectedFeature}};
        REQUIRE(sut.AddPerson(person, faces));

        auto feature = sut.QueryFaceFeature("face-005");
        REQUIRE(feature.size() == 3);
        REQUIRE(feature[0] == Catch::Approx(10.0f));
        REQUIRE(feature[1] == Catch::Approx(20.0f));
        REQUIRE(feature[2] == Catch::Approx(30.0f));
    }
}

TEST_CASE("PersonDaoService: transaction control", "[person-dao]") {
    auto testDb = MakeTestDb();
    MockServiceRegistry mocks;
    ALLOW_CALL(mocks.dbSvc, GetDb()).RETURN(testDb);

    service::PersonDaoServiceImpl sut;

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
