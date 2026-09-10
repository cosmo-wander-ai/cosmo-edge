// Unit tests for MessageFaceLibHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include <unistd.h>

#include <filesystem>

#include "api/MessageFaceLibHandler.h"
#include "flow/face/FaceLib.h"
#include "flow/face/FaceManager.h"
#include "flow/face/Person.h"
#include "mock/MockFaceLibService.h"
#include "mock/MockPersonDaoService.h"
#include "mock/MockVideoFrameCodec.h"
#include "support/ScopedServiceOverride.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

struct FaceLibHandlerMocks {
    MockFaceLibService faceLibSvc;
    MockPersonDaoService personDaoSvc;
    MockVideoFrameCodec videoCodecSvc;
};

MessageFaceLibHandler MakeHandler(FaceLibHandlerMocks& mocks) {
    return MessageFaceLibHandler(static_cast<cosmo::service::IFaceLibRepo&>(mocks.faceLibSvc),
                                 static_cast<cosmo::service::IPersonRepo&>(mocks.faceLibSvc),
                                 static_cast<cosmo::service::IFaceFeature&>(mocks.faceLibSvc),
                                 mocks.personDaoSvc, mocks.faceLibSvc, mocks.videoCodecSvc);
}

}  // namespace

TEST_CASE("FaceLibHandler: QueryFaceLibInfo", "[face-lib-handler]") {
    FaceLibHandlerMocks mocks;

    ALLOW_CALL(mocks.faceLibSvc, GetAllFaceLibs()).RETURN(std::vector<FaceLibPtr>{});
    ALLOW_CALL(mocks.faceLibSvc, GetFaceLibMaxCount()).RETURN(size_t(10));

    auto handler = MakeHandler(mocks);

    Lib::MsgQueryFaceLibInfoRecv data{};
    data.pageNum  = 1;
    data.pageSize = 10;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("FaceLibHandler: ModifyFaceLib Add empty name throws", "[face-lib-handler]") {
    FaceLibHandlerMocks mocks;

    ALLOW_CALL(mocks.faceLibSvc, GetAllFaceLibs()).RETURN(std::vector<FaceLibPtr>{});
    ALLOW_CALL(mocks.faceLibSvc, GetFaceLibMaxCount()).RETURN(size_t(10));

    auto handler = MakeHandler(mocks);

    Lib::MsgModifyFaceLibRecv data{};
    data.faceLibOperation = 1;  // Add
    data.faceLib.name     = "";
    std::error_condition errc;
    REQUIRE_THROWS(handler.Handle(std::move(data), errc));
}

TEST_CASE("FaceLibHandler: DeleteFaceLib", "[face-lib-handler]") {
    FaceLibHandlerMocks mocks;

    MsgResultFaceLibInfo success{};
    success.failedFaceLibId = "lib-1";
    success.resCode         = static_cast<int>(util::ErrorEnum::Success);
    REQUIRE_CALL(mocks.personDaoSvc, Begin());
    REQUIRE_CALL(mocks.personDaoSvc, RemoveFaceLib("lib-1")).RETURN(true);
    REQUIRE_CALL(mocks.faceLibSvc, RemoveFaceLib(_)).RETURN(std::vector<MsgResultFaceLibInfo>{success});
    REQUIRE_CALL(mocks.personDaoSvc, Commit());

    auto handler = MakeHandler(mocks);

    Lib::MsgDeleteFaceLibRecv data{};
    data.faceLibIdList.emplace_back(std::string("lib-1"));
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
    REQUIRE(ret.resData.failedFaceLibList.empty());
}

TEST_CASE("FaceLibHandler: database failure never mutates the face-lib repository",
          "[face-lib-handler][consistency]") {
    FaceLibHandlerMocks mocks;
    ALLOW_CALL(mocks.faceLibSvc, GetAllFaceLibs()).RETURN(std::vector<FaceLibPtr>{});
    ALLOW_CALL(mocks.faceLibSvc, GetFaceLibMaxCount()).RETURN(size_t(10));
    REQUIRE_CALL(mocks.faceLibSvc, CreateFaceLib(_, _)).SIDE_EFFECT(_2 = "new-lib").RETURN(FaceLibPtr{});
    REQUIRE_CALL(mocks.personDaoSvc, Begin());
    REQUIRE_CALL(mocks.personDaoSvc, AddFaceLib(_)).RETURN(false);
    REQUIRE_CALL(mocks.personDaoSvc, Rollback());
    FORBID_CALL(mocks.faceLibSvc, AddFaceLib(_));

    auto handler = MakeHandler(mocks);
    Lib::MsgModifyFaceLibRecv data{};
    data.faceLibOperation = static_cast<int>(Operation::Add);
    data.faceLib.name     = "new library";
    std::error_condition errc;
    REQUIRE_THROWS(handler.Handle(std::move(data), errc));
}

TEST_CASE("FaceLibHandler: repository rejection rolls back the database", "[face-lib-handler][consistency]") {
    FaceLibHandlerMocks mocks;
    ALLOW_CALL(mocks.faceLibSvc, GetAllFaceLibs()).RETURN(std::vector<FaceLibPtr>{});
    ALLOW_CALL(mocks.faceLibSvc, GetFaceLibMaxCount()).RETURN(size_t(10));
    REQUIRE_CALL(mocks.faceLibSvc, CreateFaceLib(_, _)).SIDE_EFFECT(_2 = "new-lib").RETURN(FaceLibPtr{});
    REQUIRE_CALL(mocks.personDaoSvc, Begin());
    REQUIRE_CALL(mocks.personDaoSvc, AddFaceLib(_)).RETURN(true);
    REQUIRE_CALL(mocks.faceLibSvc, AddFaceLib(_)).RETURN(util::ErrorEnum::ExistedName);
    REQUIRE_CALL(mocks.personDaoSvc, Rollback());

    auto handler = MakeHandler(mocks);
    Lib::MsgModifyFaceLibRecv data{};
    data.faceLibOperation = static_cast<int>(Operation::Add);
    data.faceLib.name     = "duplicate";
    std::error_condition errc;
    auto result = handler.Handle(std::move(data), errc);
    REQUIRE(errc == util::ErrorEnum::ExistedName);
    REQUIRE(result.resData.faceLibId.empty());
}

TEST_CASE("FaceLibHandler: deleting one membership preserves other face libraries",
          "[face-lib-handler][consistency]") {
    FaceLibHandlerMocks mocks;
    auto face_lib = std::make_shared<FaceLib>(std::string{});
    MsgResultInfo success{};
    success.id      = "person-1";
    success.resCode = static_cast<int>(util::ErrorEnum::Success);

    REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("lib-A")).RETURN(face_lib);
    REQUIRE_CALL(mocks.personDaoSvc, Begin());
    REQUIRE_CALL(mocks.personDaoSvc, RemovePersonFromFaceLib("person-1", "lib-A")).RETURN(true);
    REQUIRE_CALL(mocks.faceLibSvc, RemovePerson(face_lib, _)).RETURN(std::vector<MsgResultInfo>{success});
    REQUIRE_CALL(mocks.personDaoSvc, Commit());

    auto handler = MakeHandler(mocks);
    Lib::MsgDeletePersonRecv data{};
    data.faceLibId = "lib-A";
    data.personIdList.push_back("person-1");
    std::error_condition errc;
    auto result = handler.Handle(std::move(data), errc);
    REQUIRE(result.failedList.empty());
}

TEST_CASE("FaceLibHandler: failed person deletion leaves memory unchanged",
          "[face-lib-handler][consistency]") {
    FaceLibHandlerMocks mocks;
    auto face_lib = std::make_shared<FaceLib>(std::string{});

    REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("lib-A")).RETURN(face_lib);
    REQUIRE_CALL(mocks.personDaoSvc, Begin());
    REQUIRE_CALL(mocks.personDaoSvc, RemovePersonFromFaceLib("person-1", "lib-A")).RETURN(false);
    REQUIRE_CALL(mocks.personDaoSvc, Rollback());
    FORBID_CALL(mocks.faceLibSvc, RemovePerson(_, _));

    auto handler = MakeHandler(mocks);
    Lib::MsgDeletePersonRecv data{};
    data.faceLibId = "lib-A";
    data.personIdList.push_back("person-1");
    std::error_condition errc;
    auto result = handler.Handle(std::move(data), errc);
    REQUIRE(result.failedList.size() == 1);
    REQUIRE(result.failedList.front().resCode == static_cast<int>(util::ErrorEnum::DatabaseFailed));
}

TEST_CASE("FaceLibHandler: clear-person database failure never clears memory",
          "[face-lib-handler][consistency]") {
    FaceLibHandlerMocks mocks;
    auto face_lib = std::make_shared<FaceLib>(std::string{});

    REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("lib-A")).RETURN(face_lib);
    REQUIRE_CALL(mocks.personDaoSvc, Begin());
    REQUIRE_CALL(mocks.personDaoSvc, ClearFaceLib("lib-A")).RETURN(false);
    REQUIRE_CALL(mocks.personDaoSvc, Rollback());
    FORBID_CALL(mocks.faceLibSvc, RemoveAllPerson(_));

    auto handler = MakeHandler(mocks);
    Lib::MsgDeletePersonRecv data{};
    data.removeAll = 1;
    data.faceLibId = "lib-A";
    std::error_condition errc;
    REQUIRE_THROWS(handler.Handle(std::move(data), errc));
}

TEST_CASE("FaceLibHandler: clear-person memory failure rolls back the database",
          "[face-lib-handler][consistency]") {
    FaceLibHandlerMocks mocks;
    auto face_lib = std::make_shared<FaceLib>(std::string{});

    REQUIRE_CALL(mocks.faceLibSvc, GetFaceLib("lib-A")).RETURN(face_lib);
    REQUIRE_CALL(mocks.personDaoSvc, Begin());
    REQUIRE_CALL(mocks.personDaoSvc, ClearFaceLib("lib-A")).RETURN(true);
    REQUIRE_CALL(mocks.faceLibSvc, RemoveAllPerson("lib-A")).RETURN(util::ErrorEnum::Failed);
    REQUIRE_CALL(mocks.personDaoSvc, Rollback());

    auto handler = MakeHandler(mocks);
    Lib::MsgDeletePersonRecv data{};
    data.removeAll = 1;
    data.faceLibId = "lib-A";
    std::error_condition errc;
    (void)handler.Handle(std::move(data), errc);
    REQUIRE(errc == util::ErrorEnum::Failed);
}

TEST_CASE("FaceLibHandler: person update rejects partially resolved face libraries",
          "[face-lib-handler][consistency]") {
    FaceLibHandlerMocks mocks;
    auto face_lib = std::make_shared<FaceLib>(std::string{});

    REQUIRE_CALL(mocks.faceLibSvc, GetFaceLibs(_)).RETURN(std::vector<FaceLibPtr>{face_lib});
    REQUIRE_CALL(mocks.faceLibSvc, IsValidSerialNumber(_, _)).RETURN(true);
    FORBID_CALL(mocks.personDaoSvc, Begin());

    auto handler = MakeHandler(mocks);
    Lib::MsgModifyFacePicLibRecv data{};
    data.personOperation = static_cast<int>(Operation::Update);
    data.personId        = "person-1";
    data.faceLibId.emplace_back(std::string("lib-A"));
    data.faceLibId.emplace_back(std::string("lib-missing"));
    std::error_condition errc;
    REQUIRE_THROWS(handler.Handle(std::move(data), errc));
}

TEST_CASE("FaceLibHandler: staged person picture reaches image processing before database commit",
          "[face-lib-handler][staged-upload]") {
    FaceLibHandlerMocks mocks;
    auto person = std::make_shared<Person>("person-1");

    REQUIRE_CALL(mocks.faceLibSvc, GetFaceLibs(_)).RETURN(std::vector<FaceLibPtr>{});
    REQUIRE_CALL(mocks.faceLibSvc, IsValidSerialNumber(_, _)).RETURN(true);
    REQUIRE_CALL(mocks.faceLibSvc, CreatePerson()).RETURN(person);
    REQUIRE_CALL(mocks.faceLibSvc, GetPersonId(person)).RETURN("person-1");
    REQUIRE_CALL(mocks.faceLibSvc, GetPersonPictures(person)).RETURN(std::vector<FacePicPtr>{});
    REQUIRE_CALL(mocks.videoCodecSvc, DecodeJpeg(_)).RETURN(nullptr);
    FORBID_CALL(mocks.personDaoSvc, Begin());
    ALLOW_CALL(mocks.faceLibSvc, ReleaseFaceModels());

    auto handler = MakeHandler(mocks);
    Lib::MsgModifyFacePicLibRecv data{};
    data.personOperation = static_cast<int>(Operation::Add);
    data.personName      = "staged upload";
    data.pictureData.push_back({0xFF, 0xD8, 0xFF});

    std::error_condition errc;
    (void)handler.Handle(std::move(data), errc);
    REQUIRE(errc == util::ErrorEnum::InternalError);
}

TEST_CASE("FaceManager: selected-person export does not require a prior query",
          "[face-lib-handler][export][consistency]") {
    FaceLibHandlerMocks mocks;
    ScopedServiceOverride<service::IPersonDaoService> personDao(mocks.personDaoSvc);
    db::FacePersonQueryResult query_result{};
    REQUIRE_CALL(mocks.personDaoSvc, QueryPersons(_)).RETURN(query_result);

    const auto output =
        std::filesystem::path("/tmp") / ("cosmo-selected-person-export-" + std::to_string(getpid()) + ".csv");
    std::error_code ec;
    std::filesystem::remove(output, ec);

    FaceManager manager;
    REQUIRE(manager.ExportPersonToPath("", {"person-1"}, output.string()) == util::ErrorEnum::Success);
    REQUIRE(std::filesystem::is_regular_file(output));
    std::filesystem::remove(output, ec);
}
