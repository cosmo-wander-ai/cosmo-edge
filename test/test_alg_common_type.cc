#include "catch_amalgamated.hpp"
/*
 * test_alg_common_type.cc - AlgCommonType unit tests
 *
 * Tests for AlgCompareTypeResult, ValidateCompareType, and ValidateClassifyGroupType.
 */
#include "flow/common/AlgCommonType.h"

using namespace cosmo;

TEST_CASE("AlgCompareTypeResult: Less comparison", "[AlgCommonType]") {
    SECTION("lValue < rValue returns true") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLess, 3, 5));
    }
    SECTION("lValue == rValue returns false") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLess, 5, 5));
    }
    SECTION("lValue > rValue returns false") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLess, 7, 5));
    }
}

TEST_CASE("AlgCompareTypeResult: Greater comparison", "[AlgCommonType]") {
    SECTION("lValue > rValue returns true") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGreater, 7, 5));
    }
    SECTION("lValue == rValue returns false") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGreater, 5, 5));
    }
    SECTION("lValue < rValue returns false") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGreater, 3, 5));
    }
}

TEST_CASE("AlgCompareTypeResult: LE comparison", "[AlgCommonType]") {
    SECTION("lValue < rValue returns true") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLE, 3, 5));
    }
    SECTION("lValue == rValue returns true") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLE, 5, 5));
    }
    SECTION("lValue > rValue returns false") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLE, 7, 5));
    }
}

TEST_CASE("AlgCompareTypeResult: GE comparison", "[AlgCommonType]") {
    SECTION("lValue > rValue returns true") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGE, 7, 5));
    }
    SECTION("lValue == rValue returns true") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGE, 5, 5));
    }
    SECTION("lValue < rValue returns false") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGE, 3, 5));
    }
}

TEST_CASE("AlgCompareTypeResult: Equal comparison", "[AlgCommonType]") {
    SECTION("lValue == rValue returns true") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeEqual, 5, 5));
    }
    SECTION("lValue != rValue returns false") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeEqual, 3, 5));
    }
}

TEST_CASE("AlgCompareTypeResult: Invalid type returns false", "[AlgCommonType]") {
    REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeInvalid, 3, 5));
    REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeMax, 3, 5));
}

TEST_CASE("AlgCompareTypeResult: Negative values return false", "[AlgCommonType]") {
    SECTION("lValue negative") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLess, -1, 5));
    }
    SECTION("rValue negative") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGreater, 3, -1));
    }
    SECTION("Both negative") {
        REQUIRE_FALSE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeEqual, -2, -3));
    }
}

TEST_CASE("AlgCompareTypeResult: Float precision", "[AlgCommonType]") {
    SECTION("Float less") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeLess, 0.1f, 0.5f));
    }
    SECTION("Float greater") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeGreater, 0.9f, 0.1f));
    }
    SECTION("Float equal") {
        REQUIRE(AlgCompareTypeResult(AlgCompareType::AlgCompareTypeEqual, 1.0f, 1.0f));
    }
}

TEST_CASE("ValidateCompareType: Boundary checks", "[AlgCommonType]") {
    SECTION("Valid range") {
        REQUIRE(ValidateCompareType(static_cast<int>(AlgCompareType::AlgCompareTypeLess)));
        REQUIRE(ValidateCompareType(static_cast<int>(AlgCompareType::AlgCompareTypeGreater)));
        REQUIRE(ValidateCompareType(static_cast<int>(AlgCompareType::AlgCompareTypeLE)));
        REQUIRE(ValidateCompareType(static_cast<int>(AlgCompareType::AlgCompareTypeGE)));
        REQUIRE(ValidateCompareType(static_cast<int>(AlgCompareType::AlgCompareTypeEqual)));
    }
    SECTION("Invalid: below range") {
        REQUIRE_FALSE(ValidateCompareType(static_cast<int>(AlgCompareType::AlgCompareTypeInvalid)));
    }
    SECTION("Invalid: at max") {
        REQUIRE_FALSE(ValidateCompareType(static_cast<int>(AlgCompareType::AlgCompareTypeMax)));
    }
    SECTION("Invalid: above max") {
        REQUIRE_FALSE(ValidateCompareType(100));
    }
}

TEST_CASE("ValidateClassifyGroupType: Boundary checks", "[AlgCommonType]") {
    SECTION("Valid range") {
        REQUIRE(ValidateClassifyGroupType(static_cast<int>(AlgClassifyGroupType::AlgClassifyGroupTypeNone)));
        REQUIRE(ValidateClassifyGroupType(
            static_cast<int>(AlgClassifyGroupType::AlgClassifyGroupTypeAreaTargetCount)));
    }
    SECTION("Invalid: below range") {
        REQUIRE_FALSE(ValidateClassifyGroupType(-1));
    }
    SECTION("Invalid: at max") {
        REQUIRE_FALSE(
            ValidateClassifyGroupType(static_cast<int>(AlgClassifyGroupType::AlgClassifyGroupTypeMax)));
    }
}
