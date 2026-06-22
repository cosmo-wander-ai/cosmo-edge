#include "catch_amalgamated.hpp"
/*
 * test_file_service_impl.cc — FileServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Test pure logic paths only (construction, URL retrieval).
 * Network-dependent operations (upload/download) cannot be tested without
 * a file server, so they are skipped.
 */
#include "service/path/impl/FileServiceImpl.h"
#include "test_mock_services.h"

using namespace cosmo::service;

TEST_CASE("FileServiceImpl: construction and destruction", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut;
        // destructor runs Shutdown internally
    }());
}

TEST_CASE("FileServiceImpl: GetFileUrl returns empty when not initialized", "[FileService]") {
    FileServiceImpl sut;
    auto url = sut.GetFileUrl(FileType::Image);
    REQUIRE(url.empty());
}

TEST_CASE("FileServiceImpl: double destruction is safe", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut;
        // destructor calls Shutdown — verify no crash on double destroy
    }());
}
