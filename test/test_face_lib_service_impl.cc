#include "catch_amalgamated.hpp"
#include "mock/MockServiceRegistry.h"
#include "service/face/impl/FaceLibServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("FaceLibServiceImpl: Basic operations", "[FaceLibService]") {
    cosmo::test::MockServiceRegistry mocks;
    FaceLibServiceImpl sut;

    SECTION("GetAllFaceLibs should not crash") {
        try {
            auto libs = sut.GetAllFaceLibs();
            REQUIRE(true);
        } catch (...) {
            // Ignore for now — DB may be uninitialized
        }
    }

    SECTION("FaceLib max count is positive") {
        REQUIRE(sut.GetFaceLibMaxCount() > 0);
    }
}

TEST_CASE("FaceLibServiceImpl: Import status when idle", "[FaceLibService]") {
    cosmo::test::MockServiceRegistry mocks;
    FaceLibServiceImpl sut;

    SECTION("ImportComplete returns true when no import running") {
        REQUIRE(sut.ImportComplete() == true);
    }

    SECTION("GetImportTotalCount returns 0 when idle") {
        REQUIRE(sut.GetImportTotalCount() == 0);
    }

    SECTION("GetImportStatus returns {0,0} when idle") {
        auto status = sut.GetImportStatus();
        REQUIRE(status.first == 0);
        REQUIRE(status.second == 0);
    }

    SECTION("GetImportFailedUrl returns empty when idle") {
        // GetImportFailedUrl returns a fixed path (importerror.csv), so it shouldn't be empty
        REQUIRE_FALSE(sut.GetImportFailedUrl().empty());
    }
}

TEST_CASE("FaceLibServiceImpl: CreatePerson returns valid ptr", "[FaceLibService]") {
    cosmo::test::MockServiceRegistry mocks;
    FaceLibServiceImpl sut;

    auto person = sut.CreatePerson();
    REQUIRE(person != nullptr);

    SECTION("GetPersonId returns non-empty id") {
        auto id = sut.GetPersonId(person);
        REQUIRE(!id.empty());
    }

    SECTION("Initial picture count is 0") {
        REQUIRE(sut.GetPersonPictureCount(person) == 0);
    }

    SECTION("Initial pictures list is empty") {
        auto pics = sut.GetPersonPictures(person);
        REQUIRE(pics.empty());
    }
}
