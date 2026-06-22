#include "catch_amalgamated.hpp"
#include "service/network/impl/DeviceDiscoveryServiceImpl.h"
#include "test_mock_services.h"

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
