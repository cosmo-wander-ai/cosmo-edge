#include "catch_amalgamated.hpp"
/// @file test_condition_builder.cc
/// @brief ConditionBuilder unit tests — validates WHERE clause generation
///        and bind-value collection without any database dependency.

#include "db/ConditionBuilder.h"

namespace cosmo::test {

TEST_CASE("ConditionBuilder: empty builder produces no WHERE clause", "[condition-builder]") {
    db::ConditionBuilder cb;

    SECTION("Empty() returns true") {
        REQUIRE(cb.Empty());
    }

    SECTION("BuildWhereClause returns empty string") {
        REQUIRE(cb.BuildWhereClause().empty());
    }
}

TEST_CASE("ConditionBuilder: AddEqual string skips empty values", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddEqual("name", std::string{});

    REQUIRE(cb.Empty());
    REQUIRE(cb.BuildWhereClause().empty());
}

TEST_CASE("ConditionBuilder: AddEqual string with non-empty value", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddEqual("camera_id", std::string{"cam-001"});

    REQUIRE_FALSE(cb.Empty());

    auto sql = cb.BuildWhereClause();
    REQUIRE(sql == " WHERE camera_id = ?");
}

TEST_CASE("ConditionBuilder: AddEqual int always adds condition", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddEqual("status", 0);

    REQUIRE_FALSE(cb.Empty());

    auto sql = cb.BuildWhereClause();
    REQUIRE(sql == " WHERE status = ?");
}

TEST_CASE("ConditionBuilder: AddGreaterOrEqual skips zero and negative", "[condition-builder]") {
    db::ConditionBuilder cb;

    SECTION("zero is skipped") {
        cb.AddGreaterOrEqual("create_time", 0);
        REQUIRE(cb.Empty());
    }

    SECTION("negative is skipped") {
        cb.AddGreaterOrEqual("create_time", -100);
        REQUIRE(cb.Empty());
    }

    SECTION("positive value is added") {
        cb.AddGreaterOrEqual("create_time", 1000);
        REQUIRE_FALSE(cb.Empty());
        REQUIRE(cb.BuildWhereClause() == " WHERE create_time >= ?");
    }
}

TEST_CASE("ConditionBuilder: AddLessThan skips zero and negative", "[condition-builder]") {
    db::ConditionBuilder cb;

    SECTION("zero is skipped") {
        cb.AddLessThan("end_time", 0);
        REQUIRE(cb.Empty());
    }

    SECTION("positive value is added") {
        cb.AddLessThan("end_time", 2000);
        REQUIRE_FALSE(cb.Empty());
        REQUIRE(cb.BuildWhereClause() == " WHERE end_time < ?");
    }
}

TEST_CASE("ConditionBuilder: AddLike skips empty and wraps with %", "[condition-builder]") {
    db::ConditionBuilder cb;

    SECTION("empty value is skipped") {
        cb.AddLike("name", "");
        REQUIRE(cb.Empty());
    }

    SECTION("non-empty value generates LIKE clause") {
        cb.AddLike("name", "test");
        auto sql = cb.BuildWhereClause();
        REQUIRE(sql == " WHERE name LIKE ?");
        // The bind value should be "%test%" — tested via BindAll in integration tests
    }
}

TEST_CASE("ConditionBuilder: AddIn with empty vector is skipped", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddIn("code", std::vector<std::string>{});

    REQUIRE(cb.Empty());
}

TEST_CASE("ConditionBuilder: AddIn with single value", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddIn("code", std::vector<std::string>{"abc"});

    auto sql = cb.BuildWhereClause();
    REQUIRE(sql == " WHERE code IN (?)");
}

TEST_CASE("ConditionBuilder: AddIn with multiple values", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddIn("code", std::vector<std::string>{"a", "b", "c"});

    auto sql = cb.BuildWhereClause();
    REQUIRE(sql == " WHERE code IN (?,?,?)");
}

TEST_CASE("ConditionBuilder: AddOr delegates to AddIn", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddOr("tag", std::vector<std::string>{"x", "y"});

    auto sql = cb.BuildWhereClause();
    REQUIRE(sql == " WHERE tag IN (?,?)");
}

TEST_CASE("ConditionBuilder: AddRaw skips empty fragments", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddRaw("");
    REQUIRE(cb.Empty());
}

TEST_CASE("ConditionBuilder: AddRaw with non-empty fragment", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddRaw("custom_col IS NOT NULL");

    auto sql = cb.BuildWhereClause();
    REQUIRE(sql == " WHERE custom_col IS NOT NULL");
}

TEST_CASE("ConditionBuilder: multiple conditions joined by AND", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddEqual("area_id", std::string{"area-1"});
    cb.AddGreaterOrEqual("create_time", int64_t{1000});
    cb.AddLessThan("create_time", int64_t{2000});

    auto sql = cb.BuildWhereClause();
    REQUIRE(sql == " WHERE area_id = ? AND create_time >= ? AND create_time < ?");
}

TEST_CASE("ConditionBuilder: AppendTo appends WHERE clause to existing SQL", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddEqual("id", std::string{"rec-001"});

    std::string sql = "SELECT * FROM events";
    cb.AppendTo(sql);

    REQUIRE(sql == "SELECT * FROM events WHERE id = ?");
}

TEST_CASE("ConditionBuilder: AppendTo with empty builder does not modify SQL", "[condition-builder]") {
    db::ConditionBuilder cb;

    std::string sql = "SELECT * FROM events";
    cb.AppendTo(sql);

    REQUIRE(sql == "SELECT * FROM events");
}

TEST_CASE("ConditionBuilder: complex mixed conditions", "[condition-builder]") {
    db::ConditionBuilder cb;
    cb.AddEqual("camera_name", std::string{"Front Door"});
    cb.AddIn("algorithm_code", std::vector<std::string>{"alg1", "alg2"});
    cb.AddLike("area_name", "lobby");
    cb.AddEqual("isreported", 1);
    cb.AddGreaterOrEqual("create_time", int64_t{500});

    auto sql = cb.BuildWhereClause();
    // Verify all conditions present and joined by AND
    REQUIRE(sql.find("camera_name = ?") != std::string::npos);
    REQUIRE(sql.find("algorithm_code IN (?,?)") != std::string::npos);
    REQUIRE(sql.find("area_name LIKE ?") != std::string::npos);
    REQUIRE(sql.find("isreported = ?") != std::string::npos);
    REQUIRE(sql.find("create_time >= ?") != std::string::npos);

    // Count AND occurrences — should be 4 (5 conditions - 1)
    size_t and_count = 0;
    size_t pos       = 0;
    while ((pos = sql.find(" AND ", pos)) != std::string::npos) {
        ++and_count;
        pos += 5;
    }
    REQUIRE(and_count == 4);
}

}  // namespace cosmo::test
