#include "catch_amalgamated.hpp"
#include "mock/MockServiceRegistry.h"
#include "service/network/impl/DeviceDiscoveryServiceImpl.h"

TEST_CASE("DeviceDiscoveryService: lifecycle safety", "[device-discovery]") {
    cosmo::test::MockServiceRegistry mocks;

    SECTION("Stop before Start is safe") {
        cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 46000);
        sut.Stop();  // Must not crash
    }

    SECTION("Double Stop is safe") {
        cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 46000);
        sut.Stop();
        sut.Stop();  // Must not crash
    }
}

TEST_CASE("DeviceDiscoveryService: construction with params", "[device-discovery]") {
    cosmo::test::MockServiceRegistry mocks;

    SECTION("Multicast address and port") {
        REQUIRE_NOTHROW([]() { cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 46000); }());
    }

    SECTION("Different port") {
        REQUIRE_NOTHROW([]() { cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 12345); }());
    }
}

TEST_CASE("DeviceDiscoveryService: Start then Stop", "[device-discovery]") {
    cosmo::test::MockServiceRegistry mocks;

    cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 46000);
    REQUIRE_NOTHROW(sut.Start());
    REQUIRE_NOTHROW(sut.Stop());
}
