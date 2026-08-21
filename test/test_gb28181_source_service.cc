#include "catch_amalgamated.hpp"

#include <string>
#include <utility>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/gb28181/impl/Gb28181SourceServiceImpl.h"
#include "service/network/IHttpClient.h"

namespace {

class StubHttpClient final : public cosmo::service::IHttpClient {
public:
    cosmo::service::HttpResponse response;
    std::string last_url;
    int get_calls{0};

    cosmo::service::HttpResponse Get(
        const std::string& url, long, long,
        const std::vector<std::pair<std::string, std::string>>&) override {
        last_url = url;
        ++get_calls;
        return response;
    }

    cosmo::service::HttpResponse Post(
        const std::string&, const std::string&, const std::string&, long, long,
        const std::vector<std::pair<std::string, std::string>>&) override {
        return {};
    }
};

class ScopedHttpClientRegistration {
public:
    explicit ScopedHttpClientRegistration(StubHttpClient& client) {
        cosmo::service::ServiceRegistry::Instance().Set<cosmo::service::IHttpClient>(&client);
    }
    ~ScopedHttpClientRegistration() {
        cosmo::service::ServiceRegistry::Instance().Set<cosmo::service::IHttpClient>(nullptr);
    }
};

}  // namespace

TEST_CASE("GB28181 source resolves device identity to local SRS media", "[gb28181]") {
    cosmo::service::Gb28181SourceServiceImpl service;
    cosmo::service::Gb28181Source source;

    REQUIRE(service.Resolve("34020000001320000001", source));
    CHECK(source.deviceId == "34020000001320000001");
    CHECK(source.logicalUrl == "gb28181://34020000001320000001");
    CHECK(source.mediaUrl == "rtmp://127.0.0.1:1936/live/34020000001320000001");

    cosmo::service::Gb28181Source canonical;
    REQUIRE(service.Resolve(source.logicalUrl, canonical));
    CHECK(canonical.deviceId == source.deviceId);

    CHECK_FALSE(service.Resolve("3402000000132000000", source));
    CHECK_FALSE(service.Resolve("gb28181://3402000000132000000x", source));
    CHECK_FALSE(service.Resolve("rtsp://34020000001320000001", source));
}

TEST_CASE("GB28181 source reports only active SRS live publishers", "[gb28181]") {
    StubHttpClient http;
    ScopedHttpClientRegistration registration(http);
    http.response = {
        200,
        R"({"code":0,"streams":[{"name":"34020000001320000001","app":"live","publish":{"active":true}},{"name":"34020000001320000002","app":"live","publish":{"active":false}},{"name":"34020000001320000003","app":"archive","publish":{"active":true}}]})"};

    cosmo::service::Gb28181SourceServiceImpl service;
    CHECK(service.IsStreamActive("gb28181://34020000001320000001"));
    CHECK_FALSE(service.IsStreamActive("34020000001320000002"));
    CHECK_FALSE(service.IsStreamActive("34020000001320000003"));
    CHECK(http.get_calls == 1);
    CHECK(http.last_url == "http://127.0.0.1:1985/api/v1/streams/?start=0&count=1000");
}

TEST_CASE("GB28181 source treats invalid SRS responses as offline", "[gb28181]") {
    StubHttpClient http;
    ScopedHttpClientRegistration registration(http);

    SECTION("non-success HTTP status") {
        http.response = {503, {}};
    }
    SECTION("malformed JSON") {
        http.response = {200, "not-json"};
    }
    SECTION("SRS error code") {
        http.response = {200, R"({"code":100,"streams":[]})"};
    }

    cosmo::service::Gb28181SourceServiceImpl service;
    CHECK_FALSE(service.IsStreamActive("34020000001320000001"));
}
