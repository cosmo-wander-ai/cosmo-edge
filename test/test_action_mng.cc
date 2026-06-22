#include "catch_amalgamated.hpp"
/*
 * test_action_mng.cc - ActionServiceImpl 单元测试
 */
#include "service/algorithm/impl/ActionServiceImpl.h"

TEST_CASE("ActionServiceImpl initial state", "[ActionServiceImpl]") {
    cosmo::service::ActionServiceImpl svc;

    SECTION("GetActionAlg for non-existent returns nullptr") {
        auto alg = svc.GetActionAlg("non_existent_code", "v0");
        REQUIRE(alg == nullptr);
    }

    SECTION("GetPicActionAlg for non-existent returns nullptr") {
        auto alg = svc.GetPicActionAlg("non_existent_code", "v0");
        REQUIRE(alg == nullptr);
    }
}
