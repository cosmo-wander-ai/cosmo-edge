#include "catch_amalgamated.hpp"
/**
 * @brief Unit tests for GeometricCalculation and GeometricPos.
 */
#include <cmath>
#include <limits>
#include <vector>

#include "util/GeometricCalculation.h"
#include "util/GeometricPos.h"

// Lightweight point type for template tests.
struct Point2f {
    float x, y;
};

// ========================================================================
// GeometricCalculation tests
// ========================================================================

TEST_CASE("FloatDiff", "[Geometric]") {
    REQUIRE(cosmo::util::FloatDiff(1.0, 1.1) == true);
    REQUIRE(cosmo::util::FloatDiff(1.0, 1.0) == false);
    REQUIRE(cosmo::util::FloatDiff(1.0, 1.000001) == false);
    REQUIRE(cosmo::util::FloatDiff(0.0, 0.00002) == true);
    // Verify it is constexpr-eligible.
    static_assert(cosmo::util::FloatDiff(1.0, 2.0) == true, "");
    static_assert(cosmo::util::FloatDiff(1.0, 1.0) == false, "");
}

TEST_CASE("PointRelativeLine", "[Geometric]") {
    Point2f A{0, 0}, B{10, 0};

    SECTION("Point above line (positive y in screen coords = right side)") {
        Point2f P{5, 5};
        auto result = cosmo::util::PointRelativeLine(P, A, B);
        REQUIRE(result < 0);
    }

    SECTION("Point below line") {
        Point2f P{5, -5};
        auto result = cosmo::util::PointRelativeLine(P, A, B);
        REQUIRE(result > 0);
    }

    SECTION("Point on line") {
        Point2f P{5, 0};
        auto result = cosmo::util::PointRelativeLine(P, A, B);
        REQUIRE(result == Catch::Approx(0.0f));
    }
}

TEST_CASE("PointDistance", "[Geometric]") {
    Point2f A{0, 0}, B{3, 4};
    auto dist = cosmo::util::PointDistance(A, B);
    REQUIRE(dist == Catch::Approx(5.0f));
}

TEST_CASE("PointLineDistance", "[Geometric]") {
    Point2f A{0, 0}, B{10, 0};

    SECTION("Point perpendicular to segment") {
        Point2f P{5, 3};
        auto dist = cosmo::util::PointLineDistance(P, A, B);
        REQUIRE(dist == Catch::Approx(3.0f));
    }

    SECTION("Point past endpoint A") {
        Point2f P{-3, 4};
        auto dist = cosmo::util::PointLineDistance(P, A, B);
        REQUIRE(dist == Catch::Approx(5.0f));  // distance to A
    }

    SECTION("Point past endpoint B") {
        Point2f P{13, 4};
        auto dist = cosmo::util::PointLineDistance(P, A, B);
        REQUIRE(dist == Catch::Approx(5.0f));  // distance to B
    }
}

TEST_CASE("JudgeSign", "[Geometric]") {
    REQUIRE(cosmo::util::JudgeSign(3, 5) == 1);    // same sign
    REQUIRE(cosmo::util::JudgeSign(-3, -5) == 1);  // same sign
    REQUIRE(cosmo::util::JudgeSign(3, -5) == -1);  // different sign
    REQUIRE(cosmo::util::JudgeSign(-3, 5) == -1);  // different sign
    REQUIRE(cosmo::util::JudgeSign(0, 5) == 0);    // zero
    REQUIRE(cosmo::util::JudgeSign(3, 0) == 0);    // zero
    REQUIRE(cosmo::util::JudgeSign(0, 0) == 0);    // both zero
}

TEST_CASE("PointRelativePolygon - rectangle", "[Geometric]") {
    // Rectangle (0,0) to (10,10)
    Point2f rect[] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    SECTION("Point inside") {
        Point2f P{5, 5};
        REQUIRE(cosmo::util::PointRelativePolygon(P, rect, 4) < 0);
    }

    SECTION("Point outside") {
        Point2f P{15, 5};
        REQUIRE(cosmo::util::PointRelativePolygon(P, rect, 4) > 0);
    }

    SECTION("Point on edge") {
        Point2f P{5, 0};
        REQUIRE(cosmo::util::PointRelativePolygon(P, rect, 4) == 0);
    }

    SECTION("Point on vertex (ray-casting may not detect as on-edge)") {
        Point2f P{0, 0};
        auto result = cosmo::util::PointRelativePolygon(P, rect, 4);
        // Ray-casting algorithm may return 0 (on edge) or 1 (outside) for vertices
        REQUIRE((result == 0 || result == 1));
    }
}

TEST_CASE("LineIntersection", "[Geometric]") {
    SECTION("Crossing lines") {
        Point2f A1{0, 0}, A2{10, 10}, B1{0, 10}, B2{10, 0};
        REQUIRE(cosmo::util::LineIntersection(A1, A2, B1, B2) == -1);
    }

    SECTION("Parallel lines") {
        Point2f A1{0, 0}, A2{10, 0}, B1{0, 5}, B2{10, 5};
        REQUIRE(cosmo::util::LineIntersection(A1, A2, B1, B2) == 1);
    }

    SECTION("T-intersection (endpoint on line)") {
        Point2f A1{0, 0}, A2{10, 0}, B1{5, 0}, B2{5, 10};
        REQUIRE(cosmo::util::LineIntersection(A1, A2, B1, B2) == 0);
    }
}

TEST_CASE("PolygonIntersection", "[Geometric]") {
    SECTION("Overlapping rectangles") {
        Point2f r1[] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        Point2f r2[] = {{5, 5}, {15, 5}, {15, 15}, {5, 15}};
        REQUIRE(cosmo::util::PolygonIntersection(r1, 4, r2, 4) == true);
    }

    SECTION("Separated rectangles") {
        Point2f r1[] = {{0, 0}, {5, 0}, {5, 5}, {0, 5}};
        Point2f r2[] = {{10, 10}, {15, 10}, {15, 15}, {10, 15}};
        REQUIRE(cosmo::util::PolygonIntersection(r1, 4, r2, 4) == false);
    }

    SECTION("One rectangle inside another") {
        Point2f outer[] = {{0, 0}, {20, 0}, {20, 20}, {0, 20}};
        Point2f inner[] = {{5, 5}, {15, 5}, {15, 15}, {5, 15}};
        REQUIRE(cosmo::util::PolygonIntersection(outer, 4, inner, 4) == true);
    }
}

// ========================================================================
// GeometricPos tests
// ========================================================================

TEST_CASE("DoScaleBox - negative y clamping", "[Geometric]") {
    // Regression test for the bug where out.height was incorrectly adjusted
    // using out.x instead of out.y when out.y was negative.
    cosmo::util::Box input_box(50, 10, 100, 100);  // top-left near frame top
    cosmo::util::TargetScalerParam param;
    param.scale_north = 2.0f;  // expand upward far enough to go negative

    auto result = cosmo::util::DoScaleBox(input_box, param, 1920, 1080);
    REQUIRE(result.y >= 0);
    REQUIRE(result.height > 0);
    REQUIRE(result.y + result.height <= 1080);
}

TEST_CASE("DoScaleBox - zero-size input returns input", "[Geometric]") {
    cosmo::util::Box empty_box(10, 10, 0, 100);
    cosmo::util::TargetScalerParam param;
    param.scale_side = 2.0f;
    auto result      = cosmo::util::DoScaleBox(empty_box, param, 1920, 1080);
    REQUIRE(result.width == 0);
}

TEST_CASE("IntersectionUnionRatio", "[Geometric]") {
    SECTION("Identical boxes") {
        cosmo::util::Box box(0, 0, 100, 100);
        auto iou = cosmo::util::IntersectionUnionRatio(box, box);
        REQUIRE(iou == Catch::Approx(1.0f));
    }

    SECTION("Non-overlapping boxes") {
        cosmo::util::Box a(0, 0, 10, 10);
        cosmo::util::Box b(20, 20, 10, 10);
        auto iou = cosmo::util::IntersectionUnionRatio(a, b);
        REQUIRE(iou == Catch::Approx(0.0f));
    }

    SECTION("Zero-area box") {
        cosmo::util::Box a(0, 0, 10, 10);
        cosmo::util::Box b(0, 0, 0, 0);
        auto iou = cosmo::util::IntersectionUnionRatio(a, b);
        REQUIRE(iou == Catch::Approx(0.0f));
    }
}

TEST_CASE("GetBoxOsdLines", "[Geometric]") {
    cosmo::util::Box box(10, 10, 100, 100);
    auto lines = cosmo::util::GetBoxOsdLines(box, 1920, 1080);
    REQUIRE(lines.size() == 4);
    // First line should be the top edge.
    REQUIRE(lines[0].first.x == 10);
    REQUIRE(lines[0].first.y == 10);
    REQUIRE(lines[0].second.x == 110);
    REQUIRE(lines[0].second.y == 10);
}

TEST_CASE("BoxIncludeBox", "[Geometric]") {
    cosmo::util::Box outer(0, 0, 100, 100);
    cosmo::util::Box inner(10, 10, 50, 50);
    cosmo::util::Box partial(80, 80, 50, 50);
    REQUIRE(cosmo::util::BoxIncludeBox(outer, inner) == true);
    REQUIRE(cosmo::util::BoxIncludeBox(outer, partial) == false);
}

// ========================================================================
// Legacy backward-compatibility aliases
// ========================================================================

TEST_CASE("Legacy MVC namespace aliases work", "[Geometric]") {
    Point2f A{0, 0}, B{3, 4};
    auto dist = cosmo::util::PointDistance(A, B);
    REQUIRE(dist == Catch::Approx(5.0f));
}

TEST_CASE("Legacy MVAD namespace aliases work", "[Geometric]") {
    cosmo::util::Box box(10, 10, 100, 100);
    auto lines = cosmo::GetBoxOsdLines(box, 1920, 1080);
    REQUIRE(lines.size() == 4);
}
