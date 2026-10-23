// Unit tests for MessageThingsLibHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "api/MessageThingsLibHandler.h"
#include "db/ArticlesReidDao.h"
#include "media/VideoFrame.h"
#include "mock/MockArticlesReidDaoService.h"
#include "mock/MockCameraService.h"
#include "mock/MockVideoFrameCodec.h"
#include "support/ScopedPathOverride.h"
#include "util/ErrorCode.h"
#include "util/PathUtil.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

struct ThingsLibHandlerMocks {
    MockArticlesReidDaoService articlesReidDaoSvc;
    MockCameraService cameraSvc;
    MockVideoFrameCodec videoCodecSvc;
};

MessageThingsLibHandler MakeHandler(ThingsLibHandlerMocks& mocks) {
    return MessageThingsLibHandler(mocks.articlesReidDaoSvc,
                                   static_cast<cosmo::service::ICameraTaskConfig&>(mocks.cameraSvc),
                                   mocks.videoCodecSvc);
}

/// Redirect cosmo::path roots to a throwaway temp dir for the test's lifetime, so the handler's
/// EnsureDir calls and file reads stay off the real /data tree. Restores defaults on destruction.
struct ThingsHandlerPathEnvironment {
    std::string tmp_dir;
    cosmo::test::ScopedPathOverride path_override;

    ThingsHandlerPathEnvironment()
        : tmp_dir("/tmp/cosmo_things_handler_" +
                  std::to_string(std::chrono::system_clock::now().time_since_epoch().count())),
          path_override(tmp_dir, tmp_dir) {
        std::filesystem::create_directories(tmp_dir);
    }
    ~ThingsHandlerPathEnvironment() {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }
};

}  // namespace

TEST_CASE("ThingsLibHandler: ModifyThingsLib Add", "[things-lib-handler]") {
    ThingsLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    SECTION("Normal add creates lib") {
        REQUIRE_CALL(mocks.articlesReidDaoSvc, Begin());
        REQUIRE_CALL(mocks.articlesReidDaoSvc, AddArticlesReidLib(_)).RETURN(true);
        REQUIRE_CALL(mocks.articlesReidDaoSvc, Commit());

        ThingsLib::MsgModifyThingsLibRecv data{};
        data.thingsLibOperation = 1;  // Add
        data.thingsLib.name     = "test-things-lib";
        data.thingsLib.type     = 1;
        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(!ret.resData.thingsLibId.empty());
    }

    SECTION("Empty name throws") {
        ThingsLib::MsgModifyThingsLibRecv data{};
        data.thingsLibOperation = 1;
        data.thingsLib.name     = "";
        std::error_condition errc;
        REQUIRE_THROWS_AS(handler.Handle(std::move(data), errc), util::ErrorMessage);
    }
}

TEST_CASE("ThingsLibHandler: ModifyThingsLib Update", "[things-lib-handler]") {
    ThingsLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.articlesReidDaoSvc, Begin());
    REQUIRE_CALL(mocks.articlesReidDaoSvc, UpdateArticlesReidLib(_)).RETURN(true);
    REQUIRE_CALL(mocks.articlesReidDaoSvc, Commit());

    ThingsLib::MsgModifyThingsLibRecv data{};
    data.thingsLibOperation = 2;  // Update
    data.thingsLib.id       = "existing-id";
    data.thingsLib.name     = "updated-lib";
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.thingsLibId == "existing-id");
}

TEST_CASE("ThingsLibHandler: DeleteThingsLib", "[things-lib-handler]") {
    ThingsLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    REQUIRE_CALL(mocks.articlesReidDaoSvc, RemoveArticlesReidLib("lib-1")).RETURN(true);

    ThingsLib::MsgDeleteThingsLibRecv data{};
    data.thingsLibIdList.emplace_back(std::string("lib-1"));
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.failedThingsLibList.empty());
}

TEST_CASE("ThingsLibHandler: QueryThingsLibInfo", "[things-lib-handler]") {
    ThingsLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    db::ThingsLibQueryResult dbResult{};
    dbResult.search_all       = 1;
    dbResult.things_lib_count = 1;
    db::ThingsLibRecord rec{};
    rec.id                = "lib-1";
    rec.name              = "TestThingsLib";
    rec.max_things_number = 200;
    dbResult.things_lib_list.push_back(rec);
    REQUIRE_CALL(mocks.articlesReidDaoSvc, QueryThingsLib(_)).RETURN(dbResult);

    ThingsLib::MsgQueryThingsLibInfoRecv data{};
    data.pageNum  = 1;
    data.pageSize = 10;
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.thingsLibCount == 1);
    REQUIRE(ret.resData.thingsLibList.size() == 1);
}

TEST_CASE("ThingsLibHandler: saved pictures have queryable image URLs", "[things-lib-handler]") {
    ThingsHandlerPathEnvironment path_environment;
    ThingsLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);
    SQLite::Database database(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db::ArticlesReidDao dao(database);
    dao.CreateTable();
    db::LibInfo library{};
    library.id = "lib-1";
    REQUIRE(dao.AddArticlesReidLib(library));

    // Exercise real file storage and SQLite; only image encoding/decoding is mocked.
    const std::vector<uint8_t> jpeg{0xff, 0xd8, 0xff, 0xd9};
    // The codec mock needs only a frame handle, without allocating a pixel buffer.
    auto frame = std::make_shared<media::VideoFrame>(0, 0);
    REQUIRE_CALL(mocks.videoCodecSvc, DecodeJpeg(jpeg)).RETURN(frame);
    REQUIRE_CALL(mocks.videoCodecSvc, EncodeJpeg(frame, 95)).RETURN(jpeg);
    REQUIRE_CALL(mocks.articlesReidDaoSvc, AddArticlesReid(_, "lib-1", _, _))
        .LR_RETURN(dao.AddArticlesReid(_1, _2, _3, _4));
    REQUIRE_CALL(mocks.articlesReidDaoSvc, QueryThings(_)).LR_RETURN(dao.QueryThings(_1));

    const std::string picture_name = GENERATE("", "display-name.jpg");
    ThingsLib::MsgAddLibThingsRecv add_request{};
    add_request.thingsLibId = "lib-1";
    ThingsLib::MsgAddLibThingsRecv::ArticlesReid picture{};
    picture.pictureData = jpeg;
    picture.pictureName = picture_name;
    add_request.thingsList.push_back(std::move(picture));

    std::error_condition errc;
    const auto added = handler.Handle(std::move(add_request), errc);
    REQUIRE_FALSE(errc);
    REQUIRE(added.resData.thingsId.size() == 1);

    ThingsLib::MsgQueryThingsPicturesRecv query_request{};
    query_request.thingsLibIdList.emplace_back(std::string("lib-1"));
    query_request.pageNum  = 1;
    query_request.pageSize = 10;
    const auto result      = handler.Handle(std::move(query_request), errc);
    REQUIRE_FALSE(errc);
    REQUIRE(result.resData.totalCount == 1);
    REQUIRE(result.resData.thingsList.size() == 1);
    const auto& item = result.resData.thingsList.front();
    REQUIRE(item.id == added.resData.thingsId.front());
    REQUIRE(item.pictureName == picture_name);
    REQUIRE(item.pictureUrl == "/weblibPic/articleslib/" + item.id + ".jpg");
    const nlohmann::json response = result;
    REQUIRE(response["resData"]["thingsList"][0]["pictureUrl"] == item.pictureUrl);

    const auto saved_path = std::filesystem::path(path_environment.tmp_dir) /
                            std::filesystem::path(item.pictureUrl).relative_path();
    std::ifstream saved_file(saved_path, std::ios::binary);
    REQUIRE(saved_file.is_open());
    const std::vector<uint8_t> saved_bytes{std::istreambuf_iterator<char>(saved_file),
                                           std::istreambuf_iterator<char>()};
    REQUIRE(saved_bytes == jpeg);
}

TEST_CASE("ThingsLibHandler: AddLibThings rejects path-traversal pictureUrl", "[things-lib-handler]") {
    ThingsHandlerPathEnvironment path_environment;
    ThingsLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    // No file may be decoded or inserted when the resolved path escapes the base dir.
    FORBID_CALL(mocks.videoCodecSvc, DecodeJpeg(_));
    FORBID_CALL(mocks.articlesReidDaoSvc, AddArticlesReid(_, _, _, _));

    const std::vector<std::string> payloads{"..", "../../../../etc/passwd", "/etc/passwd",
                                            "weblibPic/../../etc/passwd"};
    for (const auto& payload : payloads) {
        ThingsLib::MsgAddLibThingsRecv data{};
        data.thingsOperation = 1;  // Add
        data.thingsLibId     = "lib-1";
        ThingsLib::MsgAddLibThingsRecv::ArticlesReid thing{};
        thing.pictureUrl = payload;
        data.thingsList.push_back(thing);

        std::error_condition errc;
        auto ret = handler.Handle(std::move(data), errc);
        REQUIRE(ret.resData.thingsId.empty());
    }
}

TEST_CASE("ThingsLibHandler: AddLibThings reaches decode for legit in-base path", "[things-lib-handler]") {
    ThingsHandlerPathEnvironment path_environment;
    ThingsLibHandlerMocks mocks;
    auto handler = MakeHandler(mocks);

    // Plant a small source file directly under the (overridden) base dir.
    const auto base_dir = cosmo::path::GetBaseDir();
    {
        std::ofstream ofs(base_dir + "/legit.jpg", std::ios::binary);
        ofs << "jpg";
    }

    // DecodeJpeg returning a null frame short-circuits the pipeline, but the requirement being met
    // proves the in-base path was accepted and read.
    REQUIRE_CALL(mocks.videoCodecSvc, DecodeJpeg(_)).RETURN(nullptr);

    ThingsLib::MsgAddLibThingsRecv data{};
    data.thingsOperation = 1;  // Add
    data.thingsLibId     = "lib-1";
    ThingsLib::MsgAddLibThingsRecv::ArticlesReid thing{};
    thing.pictureUrl = "legit.jpg";
    data.thingsList.push_back(thing);

    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(ret.resData.thingsId.empty());  // null frame -> skipped after decode
}
