#include "catch_amalgamated.hpp"
/*
 * test_http_client_impl.cc — HttpClientImpl unit tests (DEBT-T01)
 *
 * Strategy: HttpClientImpl delegates to libcurl. External network tests are
 * tagged [.network], while method-selection coverage uses a loopback server.
 */
#include <string>
#include <thread>

#include "LoopbackHttpServer.h"
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

TEST_CASE("HttpClientImpl: Get sends GET without a request body", "[HttpClient][http]") {
    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/health";

    bool served = false;
    std::string request;
    std::thread server_thread([&]() { served = server.ServeOnce(0, '\0', &request); });

    HttpClientImpl sut;
    const auto response = sut.Get(url, 2, 2);
    server_thread.join();

    REQUIRE(served);
    REQUIRE(response.statusCode == 200);
    REQUIRE(request.rfind("GET /health HTTP/", 0) == 0);
    const auto header_end = request.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    REQUIRE(request.substr(header_end + 4).empty());
}

TEST_CASE("HttpClientImpl: Post sends POST for empty and non-empty bodies", "[HttpClient][http]") {
    std::string body;
    SECTION("empty body") {
        body.clear();
    }
    SECTION("non-empty body") {
        body = R"({"event":"ready"})";
    }

    cosmo::test::LoopbackHttpServer server;
    REQUIRE(server.Start());
    const auto url = "http://127.0.0.1:" + std::to_string(server.Port()) + "/submit";

    bool served = false;
    std::string request;
    std::thread server_thread([&]() { served = server.ServeOnce(0, '\0', &request); });

    HttpClientImpl sut;
    const auto response = sut.Post(url, body, "application/json", 2, 2);
    server_thread.join();

    REQUIRE(served);
    REQUIRE(response.statusCode == 200);
    REQUIRE(request.rfind("POST /submit HTTP/", 0) == 0);
    const auto header_end = request.find("\r\n\r\n");
    REQUIRE(header_end != std::string::npos);
    REQUIRE(request.substr(header_end + 4) == body);
}
