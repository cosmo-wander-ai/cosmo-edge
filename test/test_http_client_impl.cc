#include "catch_amalgamated.hpp"
/*
 * test_http_client_impl.cc — HttpClientImpl unit tests (DEBT-T01)
 *
 * Strategy: HttpClientImpl delegates to libcurl. Network tests are
 * tagged [.network]. We can safely test construction.
 */
#include "mock/MockServiceRegistry.h"
#include "service/network/impl/HttpClientImpl.h"

using namespace cosmo::service;

TEST_CASE("HttpClientImpl: construction does not crash", "[HttpClient]") {
    REQUIRE_NOTHROW([]() { HttpClientImpl sut; }());
}

TEST_CASE("HttpClientImpl: Post to invalid URL returns error", "[HttpClient][.network]") {
    HttpClientImpl sut;
    auto resp = sut.Post("http://192.0.2.1:1/invalid", "{}", "application/json", 1, 1);
    // Should return non-200 status (connection refused or timeout)
    REQUIRE(resp.statusCode != 200);
}

TEST_CASE("HttpClientImpl: Post with empty data", "[HttpClient][.network]") {
    HttpClientImpl sut;
    auto resp = sut.Post("http://192.0.2.1:1/test", "", "text/plain", 1, 1);
    REQUIRE(resp.statusCode != 200);
}
