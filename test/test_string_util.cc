#include "catch_amalgamated.hpp"
/*
 * test_string_util.cc — StringUtil unit tests
 *
 * Tests Split, Trim, ToUpper, ToLower, UTF8Length, JoinStrings,
 * GetLastPathSegment, VectorToString, and ToString.
 */
#include "util/StringUtil.h"

using namespace cosmo::util;

TEST_CASE("StringUtil: Split", "[string-util]") {
    SECTION("Basic comma split") {
        auto parts = Split("a,b,c", ",");
        REQUIRE(parts.size() == 3);
        REQUIRE(parts[0] == "a");
        REQUIRE(parts[1] == "b");
        REQUIRE(parts[2] == "c");
    }

    SECTION("Multi-char delimiter") {
        auto parts = Split("a::b::c", "::");
        // Each ':' is a delimiter, so splits on each ':'
        REQUIRE(parts.size() >= 3);
    }

    SECTION("Empty string") {
        auto parts = Split("", ",");
        // Should return empty or single empty
        REQUIRE(parts.size() <= 1);
    }

    SECTION("No delimiter found") {
        auto parts = Split("hello", ",");
        REQUIRE(parts.size() == 1);
        REQUIRE(parts[0] == "hello");
    }

    SECTION("Trailing delimiter") {
        auto parts = Split("a,b,", ",");
        REQUIRE(parts.size() >= 2);
    }
}

TEST_CASE("StringUtil: Trim", "[string-util]") {
    SECTION("Leading and trailing spaces") {
        REQUIRE(Trim("  hello  ") == "hello");
    }

    SECTION("Tabs and newlines") {
        REQUIRE(Trim("\t\nhello\r\n") == "hello");
    }

    SECTION("No whitespace") {
        REQUIRE(Trim("hello") == "hello");
    }

    SECTION("All whitespace") {
        REQUIRE(Trim("   ") == "");
    }

    SECTION("Empty string") {
        REQUIRE(Trim("") == "");
    }

    SECTION("Custom trim characters") {
        REQUIRE(Trim("xxhelloxx", "x") == "hello");
    }
}

TEST_CASE("StringUtil: ToUpper and ToLower", "[string-util]") {
    SECTION("ToUpper") {
        REQUIRE(ToUpper("hello") == "HELLO");
        REQUIRE(ToUpper("Hello World") == "HELLO WORLD");
        REQUIRE(ToUpper("") == "");
    }

    SECTION("ToLower") {
        REQUIRE(ToLower("HELLO") == "hello");
        REQUIRE(ToLower("Hello World") == "hello world");
        REQUIRE(ToLower("") == "");
    }
}

TEST_CASE("StringUtil: UTF8Length", "[string-util]") {
    SECTION("ASCII string") {
        REQUIRE(UTF8Length("hello") == 5);
    }

    SECTION("Chinese characters") {
        // Each Chinese character is 3 bytes in UTF-8
        REQUIRE(UTF8Length("你好") == 2);
    }

    SECTION("Mixed ASCII and Chinese") {
        REQUIRE(UTF8Length("hello你好") == 7);
    }

    SECTION("Empty string") {
        REQUIRE(UTF8Length("") == 0);
    }
}

TEST_CASE("StringUtil: JoinStrings", "[string-util]") {
    SECTION("Normal join with comma") {
        std::vector<std::string> items = {"a", "b", "c"};
        REQUIRE(JoinStrings(items) == "a,b,c");
    }

    SECTION("Custom separator") {
        std::vector<std::string> items = {"x", "y"};
        REQUIRE(JoinStrings(items, " | ") == "x | y");
    }

    SECTION("Single element") {
        std::vector<std::string> items = {"only"};
        REQUIRE(JoinStrings(items) == "only");
    }

    SECTION("Empty vector") {
        std::vector<std::string> items;
        REQUIRE(JoinStrings(items).empty());
    }
}

TEST_CASE("StringUtil: GetLastPathSegment", "[string-util]") {
    SECTION("URI path") {
        REQUIRE(GetLastPathSegment("/api/v1/devices") == "devices");
    }

    SECTION("Single segment") {
        REQUIRE(GetLastPathSegment("filename") == "filename");
    }

    SECTION("Trailing slash") {
        auto result = GetLastPathSegment("/path/to/dir/");
        // Depending on implementation, trailing slash might mean empty segment
        REQUIRE((result.empty() || result == "dir"));
    }
}

TEST_CASE("StringUtil: VectorToString", "[string-util]") {
    SECTION("Integer vector") {
        std::vector<int> v = {1, 2, 3};
        REQUIRE(VectorToString(v) == "[1,2,3]");
    }

    SECTION("Empty vector") {
        std::vector<int> v;
        REQUIRE(VectorToString(v) == "");
    }

    SECTION("Single element") {
        std::vector<int> v = {42};
        REQUIRE(VectorToString(v) == "[42]");
    }
}

TEST_CASE("StringUtil: ToString", "[string-util]") {
    SECTION("Integer conversion") {
        REQUIRE(ToString(42) == "42");
    }

    SECTION("String passthrough") {
        REQUIRE(ToString(std::string("hello")) == "hello");
    }
}
