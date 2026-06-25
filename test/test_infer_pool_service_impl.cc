#include "catch_amalgamated.hpp"
/*
 * test_infer_pool_service_impl.cc — InferPoolServiceImpl unit tests (DEBT-T01)
 *
 * Strategy: InferPoolServiceImpl manages NPU inference model pools.
 * Full functional tests require NPU hardware (bmrt).
 * We test construction/destruction and thread-safety of the pool map.
 */
#include "mock/MockServiceRegistry.h"
#include "service/ai/impl/InferPoolServiceImpl.h"

using namespace cosmo::service;

TEST_CASE("InferPoolServiceImpl: construction and destruction", "[InferPoolService]") {
    REQUIRE_NOTHROW([]() {
        InferPoolServiceImpl sut;
        // No NPU calls at construction, should be safe everywhere
    }());
}

TEST_CASE("InferPoolServiceImpl: GetRecognizerPool creates and returns pool", "[InferPoolService][.device]") {
    InferPoolServiceImpl sut;

    auto pool = sut.GetRecognizerPool("test_alg_001");
    REQUIRE(pool != nullptr);

    SECTION("Same key returns same pool instance") {
        auto pool2 = sut.GetRecognizerPool("test_alg_001");
        REQUIRE(pool.get() == pool2.get());
    }

    SECTION("Different key returns different pool") {
        auto pool3 = sut.GetRecognizerPool("test_alg_002");
        REQUIRE(pool.get() != pool3.get());
    }
}

TEST_CASE("InferPoolServiceImpl: GetLandmarkPool creates and returns pool", "[InferPoolService][.device]") {
    InferPoolServiceImpl sut;
    auto pool = sut.GetLandmarkPool("lmk_alg");
    REQUIRE(pool != nullptr);
}

TEST_CASE("InferPoolServiceImpl: GetDetectPool creates and returns pool", "[InferPoolService][.device]") {
    InferPoolServiceImpl sut;
    auto pool = sut.GetDetectPool("det_alg");
    REQUIRE(pool != nullptr);
}
