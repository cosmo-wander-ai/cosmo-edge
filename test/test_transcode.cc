#include "catch_amalgamated.hpp"
/*
 * test_transcode.cc — Transcode unit tests
 *
 * Tests iconv-based character encoding conversion (GB18030 → UTF-8, etc.).
 */
#include "util/Transcode.h"

using namespace cosmo::util;

TEST_CASE("Transcode: UTF-8 to UTF-8 passthrough", "[transcode]") {
    Transcode conv("UTF-8", "UTF-8");
    std::string input = "Hello, World!";
    auto result       = conv.Convert(input);
    REQUIRE(result == input);
}

TEST_CASE("Transcode: empty string", "[transcode]") {
    Transcode conv("UTF-8", "UTF-8");
    auto result = conv.Convert("");
    REQUIRE(result.empty());
}

TEST_CASE("Transcode: UTF-8 to ASCII (pure ASCII input)", "[transcode]") {
    // ASCII is a subset of UTF-8, so pure ASCII should convert fine
    Transcode conv("ASCII", "UTF-8");
    auto result = conv.Convert("hello");
    REQUIRE(result == "hello");
}

TEST_CASE("Transcode: move constructor", "[transcode]") {
    Transcode conv("UTF-8", "UTF-8");
    Transcode conv2(std::move(conv));
    auto result = conv2.Convert("test");
    REQUIRE(result == "test");
}

TEST_CASE("Transcode: move assignment", "[transcode]") {
    Transcode conv1("UTF-8", "UTF-8");
    Transcode conv2("ASCII", "UTF-8");
    conv2       = std::move(conv1);
    auto result = conv2.Convert("test");
    REQUIRE(result == "test");
}
