// Unit tests for MessageFaceLibHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageFaceLibHandler.h"
#include "flow/face/FaceLib.h"
#include "mock/MockFaceLibService.h"
#include "mock/MockPersonDaoService.h"
#include "mock/MockServiceRegistry.h"
#include "mock/MockVideoFrameCodec.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

namespace {

MessageFaceLibHandler MakeHandler(MockServiceRegistry& mocks) {
    return MessageFaceLibHandler(static_cast<cosmo::service::IFaceLibRepo&>(mocks.faceLibSvc),
                                 static_cast<cosmo::service::IPersonRepo&>(mocks.faceLibSvc),
                                 static_cast<cosmo::service::IFaceFeature&>(mocks.faceLibSvc),
                                 mocks.personDaoSvc, mocks.faceLibSvc, mocks.videoCodecSvc);
}

}  // namespace

TEST_CASE("FaceLibHandler: QueryFaceLibInfo", "[face-lib-handler]") {
    MockServiceRegistry mocks;

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
    MockServiceRegistry mocks;

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
    MockServiceRegistry mocks;

    ALLOW_CALL(mocks.faceLibSvc, GetAllFaceLibs()).RETURN(std::vector<FaceLibPtr>{});
    ALLOW_CALL(mocks.faceLibSvc, GetFaceLibMaxCount()).RETURN(size_t(10));
    ALLOW_CALL(mocks.faceLibSvc, RemoveFaceLib(_)).RETURN(std::vector<MsgResultFaceLibInfo>{});

    auto handler = MakeHandler(mocks);

    Lib::MsgDeleteFaceLibRecv data{};
    data.faceLibIdList.emplace_back(std::string("lib-1"));
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}
