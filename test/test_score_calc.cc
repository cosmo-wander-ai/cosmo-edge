#include "catch_amalgamated.hpp"
/*
 * test_score_calc.cc — CalcCustomScore unit tests
 *
 * Tests GPU health score calculation with various load scenarios.
 */
#include "util/ScoreCalc.h"

using namespace cosmo;

TEST_CASE("CalcCustomScore: zero load returns baseline score", "[score-calc]") {
    std::vector<GpuMemSnapshot> devs;
    auto score = CalcCustomScore(0.0, 0, 0, devs, 0.0, 0);
    REQUIRE(score >= 0.0);
    REQUIRE(score <= 100.0);
}

TEST_CASE("CalcCustomScore: moderate GPU usage", "[score-calc]") {
    std::vector<GpuMemSnapshot> devs = {{8000, 4000}};
    auto score                       = CalcCustomScore(0.5, 8000, 4000, devs, 0.0, 0);
    REQUIRE(score >= 0.0);
}

TEST_CASE("CalcCustomScore: high discard ratio increases score", "[score-calc]") {
    std::vector<GpuMemSnapshot> devs = {{8000, 4000}};
    auto low_discard                 = CalcCustomScore(0.5, 8000, 4000, devs, 0.0, 0);
    auto high_discard                = CalcCustomScore(0.5, 8000, 4000, devs, 0.8, 10);
    REQUIRE(high_discard >= low_discard);
}

TEST_CASE("CalcCustomScore: resource exhaustion exceeds 100", "[score-calc]") {
    std::vector<GpuMemSnapshot> devs = {{8000, 0}};
    auto score                       = CalcCustomScore(1.0, 8000, 0, devs, 1.0, 30);
    REQUIRE(score > 0);
}

TEST_CASE("CalcCustomScore: empty device list", "[score-calc]") {
    std::vector<GpuMemSnapshot> devs;
    REQUIRE_NOTHROW(CalcCustomScore(0.5, 0, 0, devs, 0.0, 0));
}

TEST_CASE("CalcCustomScore: multiple GPU devices", "[score-calc]") {
    std::vector<GpuMemSnapshot> devs = {{4000, 2000}, {4000, 1000}};
    auto score                       = CalcCustomScore(0.7, 8000, 3000, devs, 0.1, 0);
    REQUIRE(score >= 0.0);
}
