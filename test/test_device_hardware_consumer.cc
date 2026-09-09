#include "catch_amalgamated.hpp"
#include "mock/MockDeviceInfoService.h"
#include "platform/NetCardOp.h"
#include "service/network/impl/NetworkConfigServiceImpl.h"
#include "support/ScopedServiceOverride.h"

TEST_CASE("Network configuration reads identity through hardware interface", "[device-hardware]") {
    using namespace cosmo;
    test::MockDeviceInfoService device;
    test::ScopedServiceOverride<service::IDeviceHardware> hardware(device);
    REQUIRE_FALSE(service::ServiceRegistry::Instance().Has<service::IDeviceInfoService>());
    std::vector<std::pair<std::string, std::string>> macs{
        {platform::kNetworkSubEthName, "02:00:00:00:00:02"},
        {"unused", "02:00:00:00:00:03"},
        {platform::kNetworkMainEthName, "02:00:00:00:00:01"}};
    REQUIRE_CALL(device, GetMacs()).RETURN(macs);
    service::NetworkConfigServiceImpl network;
    const auto main = network.GetCardRealInfo(true);
    const auto sub  = network.GetCardRealInfo(false);
    REQUIRE(main.mac == "02:00:00:00:00:01");
    REQUIRE(main.eth_name == platform::kNetworkMainEthName);
    REQUIRE(main.is_main);
    REQUIRE(sub.mac == "02:00:00:00:00:02");
    REQUIRE(sub.eth_name == platform::kNetworkSubEthName);
    REQUIRE_FALSE(sub.is_main);
}
