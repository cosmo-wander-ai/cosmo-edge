#include "catch_amalgamated.hpp"
/*
 * test_memory_pool_service_impl.cc — MemoryPoolServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: MemoryPoolServiceImpl wraps VPU device memory (bmlib).
 * Construction requires bmlib device init which only works on aarch64.
 * All tests tagged [.device].
 */
#include "mock/MockServiceRegistry.h"
#include "service/infra/impl/MemoryPoolServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("MemoryPoolServiceImpl: construction and destruction", "[MemoryPoolService][.device]") {
    REQUIRE_NOTHROW([]() { MemoryPoolServiceImpl sut; }());
}

TEST_CASE("MemoryPoolServiceImpl: Status returns pool info", "[MemoryPoolService][.device]") {
    MemoryPoolServiceImpl sut;
    auto status = sut.Status();
    // Should return at least some pool buckets
    REQUIRE(!status.empty());
}

TEST_CASE("MemoryPoolServiceImpl: OutputMallocBuf returns non-empty string", "[MemoryPoolService][.device]") {
    MemoryPoolServiceImpl sut;
    auto buf = sut.OutputMallocBuf();
    REQUIRE(!buf.empty());
}

TEST_CASE("MemoryPoolServiceImpl: Acquire and Recycle", "[MemoryPoolService][.device]") {
    MemoryPoolServiceImpl sut;

    SECTION("Acquire returns non-null block for valid size") {
        auto* block = sut.Acquire(1024);
        REQUIRE(block != nullptr);
        sut.Recycle(block);
    }

    SECTION("Recycle nullptr does not crash") {
        REQUIRE_NOTHROW(sut.Recycle(nullptr));
    }
}
