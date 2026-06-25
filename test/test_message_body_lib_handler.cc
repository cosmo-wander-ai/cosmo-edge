// Unit tests for MessageBodyLibHandler
// Tests DI-injected handler with mock services for all 9 Handle methods.

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageBodyLibHandler.h"
#include "mock/MockBodyLibService.h"
#include "mock/MockCameraService.h"
#include "mock/MockPersonRecogDaoService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockVideoFrameCodec.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

/// Helper: create a handler with mock references from MockServiceRegistry.
MessageBodyLibHandler MakeHandler(MockServiceRegistry& mocks) {
    return MessageBodyLibHandler(mocks.personRecogDaoSvc, mocks.bodyLibSvc, mocks.cameraSvc,
                                 mocks.videoCodecSvc);
}

}  // namespace

TEST_CASE("BodyLibHandler: ModifyPersonLib Add", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    SECTION("Normal add creates lib and invalidates cache") {
        REQUIRE_CALL(mocks.personRecogDaoSvc, Begin());
        REQUIRE_CALL(mocks.personRecogDaoSvc, AddPersonLib(_)).RETURN(true);
        REQUIRE_CALL(mocks.personRecogDaoSvc, Commit());
        REQUIRE_CALL(mocks.bodyLibSvc, InvalidateCache(_));

        BodyLib::MsgModifyPersonLibRecv data{};
        data.personLibOperation = 1;  // Add
        data.personLib.name     = "test-lib";
        data.personLib.type     = 1;
        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(!ret.resData.personLibId.empty());
        REQUIRE(!errc);
    }

    SECTION("Empty name throws ParameterException") {
        BodyLib::MsgModifyPersonLibRecv data{};
        data.personLibOperation = 1;
        data.personLib.name     = "";
        std::error_condition errc;
        REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
    }

    SECTION("DB failure throws DatabaseFailed") {
        REQUIRE_CALL(mocks.personRecogDaoSvc, Begin());
        REQUIRE_CALL(mocks.personRecogDaoSvc, AddPersonLib(_)).RETURN(false);
        REQUIRE_CALL(mocks.personRecogDaoSvc, Rollback());

        BodyLib::MsgModifyPersonLibRecv data{};
        data.personLibOperation = 1;
        data.personLib.name     = "fail-lib";
        std::error_condition errc;
        REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
    }
}

TEST_CASE("BodyLibHandler: ModifyPersonLib Update", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.personRecogDaoSvc, Begin());
    REQUIRE_CALL(mocks.personRecogDaoSvc, UpdatePersonLib(_)).RETURN(true);
    REQUIRE_CALL(mocks.personRecogDaoSvc, Commit());
    REQUIRE_CALL(mocks.bodyLibSvc, InvalidateCache(_));

    BodyLib::MsgModifyPersonLibRecv data{};
    data.personLibOperation = 2;  // Update
    data.personLib.id       = "existing-id";
    data.personLib.name     = "updated-lib";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.personLibId == "existing-id");
}

TEST_CASE("BodyLibHandler: DeletePersonLib", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    SECTION("Partial failure returns failed list") {
        REQUIRE_CALL(mocks.personRecogDaoSvc, RemovePersonLib("lib-ok")).RETURN(true);
        REQUIRE_CALL(mocks.personRecogDaoSvc, RemovePersonLib("lib-fail")).RETURN(false);
        ALLOW_CALL(mocks.bodyLibSvc, InvalidateCache(_));

        BodyLib::MsgDeletePersonLibRecv data{};
        data.personLibIdList.emplace_back(std::string("lib-ok"));
        data.personLibIdList.emplace_back(std::string("lib-fail"));
        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(ret.resData.failedPersonLibList.size() == 1);
    }
}

TEST_CASE("BodyLibHandler: QueryPersonLibInfo", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    db::PersonRecogLibQueryResult dbResult{};
    dbResult.search_all       = 1;
    dbResult.person_lib_count = 1;
    db::PersonRecogLibRecord rec{};
    rec.id                = "lib-1";
    rec.name              = "TestLib";
    rec.max_person_number = 100;
    dbResult.person_lib_list.push_back(rec);
    REQUIRE_CALL(mocks.personRecogDaoSvc, QueryPersonLib(_)).RETURN(dbResult);

    BodyLib::MsgQueryPersonLibInfoRecv data{};
    data.pageNum  = 1;
    data.pageSize = 10;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.personLibCount == 1);
    REQUIRE(ret.resData.personLibList.size() == 1);
    REQUIRE(ret.resData.personLibList[0].id == "lib-1");
}

TEST_CASE("BodyLibHandler: QueryPersonPictures bad pagination", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    BodyLib::MsgQueryPersonPicturesRecv data{};
    data.pageNum  = 0;
    data.pageSize = 0;
    std::error_condition errc;
    REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
}

TEST_CASE("BodyLibHandler: BindTaskPersonLib searchAll", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.personRecogDaoSvc, GetAllPersonLibs()).RETURN(std::vector<std::string>{"l1", "l2"});
    REQUIRE_CALL(mocks.cameraSvc, BindTaskLibPara(std::string("cam-1"), std::string("alg-1"),
                                                  std::vector<std::string>{"l1", "l2"}, _))
        .RETURN(cosmo::util::ErrorEnum::Success);

    BodyLib::MsgBindTaskPersonLibRecv data{};
    data.searchAll     = 1;
    data.cameraId      = "cam-1";
    data.algorithmCode = "alg-1";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.failedPersonLibList.empty());
}

TEST_CASE("BodyLibHandler: DeleteLibPerson removeAll", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    SECTION("removeAll with specific libId") {
        REQUIRE_CALL(mocks.personRecogDaoSvc, ClearPersonLib(std::string("lib-x"))).RETURN(true);

        BodyLib::MsgDeleteLibPersonRecv data{};
        data.removeAll   = 1;
        data.personLibId = "lib-x";
        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(!errc);
    }

    SECTION("removeAll without libId clears all") {
        REQUIRE_CALL(mocks.personRecogDaoSvc, GetAllPersonLibs()).RETURN(std::vector<std::string>{"a", "b"});
        REQUIRE_CALL(mocks.personRecogDaoSvc, ClearPersonLib(std::string("a"))).RETURN(true);
        REQUIRE_CALL(mocks.personRecogDaoSvc, ClearPersonLib(std::string("b"))).RETURN(true);

        BodyLib::MsgDeleteLibPersonRecv data{};
        data.removeAll = 1;
        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(!errc);
    }
}

TEST_CASE("BodyLibHandler: DetectPerson empty image", "[body-lib-handler]") {
    MockServiceRegistry mocks;
    auto handler = MakeHandler(mocks);

    BodyLib::MsgDetectPersonRecv data{};
    data.imageBase64 = "";
    std::error_condition errc;
    REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
}
