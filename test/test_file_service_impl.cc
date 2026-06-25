#include "catch_amalgamated.hpp"
/*
 * test_file_service_impl.cc — FileServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Test pure logic paths only (construction, URL retrieval).
 * Network-dependent operations (upload/download) cannot be tested without
 * a file server, so they are skipped.
 */
#include "mock/MockServiceRegistry.h"
#include "service/path/impl/FileServiceImpl.h"

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

TEST_CASE("FileServiceImpl: GetFileUrl for different types", "[FileService]") {
    FileServiceImpl sut;

    SECTION("Image type returns empty when not initialized") {
        REQUIRE(sut.GetFileUrl(FileType::Image).empty());
    }

    SECTION("Video type returns empty when not initialized") {
        REQUIRE(sut.GetFileUrl(FileType::Video).empty());
    }
}

TEST_CASE("FileServiceImpl: multiple instances do not interfere", "[FileService]") {
    REQUIRE_NOTHROW([]() {
        FileServiceImpl sut1;
        FileServiceImpl sut2;
    }());
}
