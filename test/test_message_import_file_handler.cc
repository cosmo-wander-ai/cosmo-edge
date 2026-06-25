// Unit tests for MessageImportFileHandler

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on

#include "api/MessageImportFileHandler.h"
#include "mock/MockFaceLibService.h"
#include "mock/MockServiceRegistry.h"
#include "util/ErrorCode.h"

using namespace cosmo;
using namespace cosmo::test;
using trompeloeil::_;

TEST_CASE("ImportFileHandler: construction", "[import-file-handler]") {
    // MessageImportFileHandler has default constructor
    REQUIRE_NOTHROW([]() { MessageImportFileHandler handler; }());
}

TEST_CASE("ImportFileHandler: QueryImportStatus", "[import-file-handler]") {
    MockServiceRegistry mocks;
    MessageImportFileHandler handler;

    REQUIRE_CALL(mocks.faceLibSvc, GetImportStatus()).RETURN(std::make_pair(50, 100));
    REQUIRE_CALL(mocks.faceLibSvc, ImportComplete()).RETURN(false);
    REQUIRE_CALL(mocks.faceLibSvc, GetImportTotalCount()).RETURN(100);
    ALLOW_CALL(mocks.faceLibSvc, GetImportFailedUrl()).RETURN("");

    service::MsgQueryImportStatusRecv data{};
    data.importType = 2;  // Face import type
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}

TEST_CASE("ImportFileHandler: QueryImportStatus when complete", "[import-file-handler]") {
    MockServiceRegistry mocks;
    MessageImportFileHandler handler;

    REQUIRE_CALL(mocks.faceLibSvc, GetImportStatus()).RETURN(std::make_pair(100, 100));
    REQUIRE_CALL(mocks.faceLibSvc, ImportComplete()).RETURN(true);
    REQUIRE_CALL(mocks.faceLibSvc, GetImportTotalCount()).RETURN(100);
    ALLOW_CALL(mocks.faceLibSvc, GetImportFailedUrl()).RETURN("");

    service::MsgQueryImportStatusRecv data{};
    data.importType = 2;  // Face import type
    std::error_condition errc;
    auto ret = handler.Handle(std::move(data), errc);
    REQUIRE(!errc);
}
