#include "catch_amalgamated.hpp"
/*
 * test_storage_clean_service_impl.cc — StorageCleanServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: Test lifecycle safety (Start/Stop idempotency).
 * Actual cleanup logic depends on StorageSpace and file system state,
 * so we only verify Start/Stop contract and crash safety.
 */
#include "mock/MockServiceRegistry.h"
#include "service/infra/impl/StorageCleanServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("StorageCleanServiceImpl: construction and destruction", "[StorageCleanService]") {
    REQUIRE_NOTHROW([]() { StorageCleanServiceImpl sut; }());
}

TEST_CASE("StorageCleanServiceImpl: Start then Stop lifecycle", "[StorageCleanService]") {
    cosmo::test::MockServiceRegistry mocks;

    StorageCleanServiceImpl sut;

    SECTION("Start then Stop does not crash") {
        REQUIRE_NOTHROW(sut.Start());
        REQUIRE_NOTHROW(sut.Stop());
    }

    SECTION("Double stop is safe") {
        sut.Start();
        REQUIRE_NOTHROW(sut.Stop());
        REQUIRE_NOTHROW(sut.Stop());
    }

    SECTION("Stop without start is safe") {
        REQUIRE_NOTHROW(sut.Stop());
    }
}

TEST_CASE("StorageCleanServiceImpl: destructor calls Stop", "[StorageCleanService]") {
    cosmo::test::MockServiceRegistry mocks;

    REQUIRE_NOTHROW([]() {
        StorageCleanServiceImpl sut;
        sut.Start();
        // destructor should call Stop
    }());
}
