#include <string>

#include "catch_amalgamated.hpp"
#include "util/HttpUrlUtil.h"

TEST_CASE("HttpUrlUtil: accepts supported HTTP endpoints", "[http-url]") {
    REQUIRE(cosmo::util::IsValidHttpUrl("http://example.com"));
    REQUIRE(cosmo::util::IsValidHttpUrl("https://example.com"));
    REQUIRE(cosmo::util::IsValidHttpUrl("http://192.168.1.10:8080/push"));
    REQUIRE(cosmo::util::IsValidHttpUrl("https://[2001:db8::1]:8443/push"));
    REQUIRE(cosmo::util::IsValidHttpUrl("https://example.com/path?event=alarm#latest"));
    REQUIRE(cosmo::util::IsValidHttpUrl("http://device_name/push"));
    REQUIRE(cosmo::util::IsValidHttpUrl("http://example.com/path?recipient=user@example.com"));
}

TEST_CASE("HttpUrlUtil: enforces the endpoint length limit", "[http-url]") {
    std::string url{"https://example.com/"};
    url.append(2048 - url.size(), 'a');
    REQUIRE(cosmo::util::IsValidHttpUrl(url));

    url.push_back('a');
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl(url));
}

TEST_CASE("HttpUrlUtil: rejects unsupported or malformed endpoints", "[http-url]") {
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl(""));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("file:///etc/passwd"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("ftp://example.com/data"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("HTTP://example.com"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("http://"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("http:///push"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("http://user@example.com/push"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("http://example.com/line\nbreak"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("http://example.com\\push"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("http://example!.com/push"));
    REQUIRE_FALSE(cosmo::util::IsValidHttpUrl("http://[]:-._/push"));
}
