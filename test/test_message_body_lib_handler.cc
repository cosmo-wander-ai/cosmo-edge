// Unit tests for MessageBodyLibHandler
// Tests DI-injected handler with mock services for all 9 Handle methods.

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include <chrono>
#include <filesystem>
#include <fstream>

#include "api/MessageBodyLibHandler.h"
#include "mock/MockBodyLibService.h"
#include "mock/MockCameraService.h"
#include "mock/MockPersonRecogDaoService.h"
#include "mock/MockVideoFrameCodec.h"
#include "support/ScopedPathOverride.h"
#include "util/ErrorCode.h"
#include "util/PathUtil.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

struct BodyLibHandlerMocks {
    MockPersonRecogDaoService personRecogDaoSvc;
    MockBodyLibService bodyLibSvc;
    MockCameraService cameraSvc;
    MockVideoFrameCodec videoCodecSvc;
};

MessageBodyLibHandler MakeHandler(BodyLibHandlerMocks& mocks) {
    return MessageBodyLibHandler(mocks.personRecogDaoSvc, mocks.bodyLibSvc, mocks.cameraSvc,
                                 mocks.videoCodecSvc);
}

/// Redirect cosmo::path roots to a throwaway temp dir for the test's lifetime, so the handler's
/// EnsureDir calls and file copies stay off the real /data tree. Restores defaults on destruction.
struct BodyHandlerPathEnvironment {
    std::string tmp_dir;
    cosmo::test::ScopedPathOverride path_override;

    BodyHandlerPathEnvironment()
        : tmp_dir("/tmp/cosmo_body_handler_" +
                  std::to_string(std::chrono::system_clock::now().time_since_epoch().count())),
          path_override(tmp_dir, tmp_dir) {
        std::filesystem::create_directories(tmp_dir);
    }
    ~BodyHandlerPathEnvironment() {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }
};

}  // namespace

TEST_CASE("BodyLibHandler: ModifyPersonLib Add", "[body-lib-handler]") {
    BodyLibHandlerMocks mocks;
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
    BodyLibHandlerMocks mocks;
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
    BodyLibHandlerMocks mocks;
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
    BodyLibHandlerMocks mocks;
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
    BodyLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    BodyLib::MsgQueryPersonPicturesRecv data{};
    data.pageNum  = 0;
    data.pageSize = 0;
    std::error_condition errc;
    REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
}

TEST_CASE("BodyLibHandler: BindTaskPersonLib searchAll", "[body-lib-handler]") {
    BodyLibHandlerMocks mocks;
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
    BodyLibHandlerMocks mocks;
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
    BodyLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    BodyLib::MsgDetectPersonRecv data{};
    data.imageBase64 = "";
    std::error_condition errc;
    REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
}

TEST_CASE("BodyLibHandler: AddLibPerson rejects path-traversal pictureUrl", "[body-lib-handler]") {
    BodyHandlerPathEnvironment path_environment;
    BodyLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    // No file may be decoded or inserted when the resolved path escapes its allowed root.
    FORBID_CALL(mocks.videoCodecSvc, DecodeJpeg(_));
    FORBID_CALL(mocks.personRecogDaoSvc, AddPerson(_, _, _, _));

    const std::vector<std::string> payloads{
        "..", "../", "../../../../etc/passwd", "/etc/passwd", "weblibPic/../../etc/passwd", "/etc/shadow"};
    for (const auto& payload : payloads) {
        BodyLib::MsgAddLibPersonRecv data{};
        data.personOperation = 1;  // Add
        data.personLibId     = "lib-1";
        BodyLib::MsgAddLibPersonRecv::Person person{};
        person.pictureUrl = payload;
        data.personList.push_back(person);

        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(ret.resData.personId.empty());
    }
}

TEST_CASE("BodyLibHandler: AddLibPerson accepts legit in-root picture", "[body-lib-handler]") {
    BodyHandlerPathEnvironment path_environment;
    BodyLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    // Plant a small source file inside the person-photo dir; filename() of pictureUrl must resolve to it.
    const auto photo_dir = cosmo::path::GetPersonLibPhotoDir();
    std::filesystem::create_directories(photo_dir);
    {
        std::ofstream ofs(photo_dir + "/legit.jpg", std::ios::binary);
        ofs << "jpg";
    }

    // A null decoded frame skips feature extraction but the person is still inserted; proving the
    // in-root path was accepted, read, and decoded.
    REQUIRE_CALL(mocks.videoCodecSvc, DecodeJpeg(_)).RETURN(nullptr);
    REQUIRE_CALL(mocks.personRecogDaoSvc, AddPerson(_, _, _, _)).RETURN(true);
    REQUIRE_CALL(mocks.bodyLibSvc, InvalidateCache(_));

    BodyLib::MsgAddLibPersonRecv data{};
    data.personOperation = 1;  // Add
    data.personLibId     = "lib-1";
    BodyLib::MsgAddLibPersonRecv::Person person{};
    person.pictureUrl = "legit.jpg";
    data.personList.push_back(person);

    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.personId.size() == 1);
}
