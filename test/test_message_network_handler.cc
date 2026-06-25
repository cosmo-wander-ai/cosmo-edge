// Unit tests for MessageNetworkHandler.
// Verifies correct DTO mapping and service delegation for all 6 network API handlers.

// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "api/MessageNetworkHandler.h"
#include "mock/MockNetworkService.h"
#include "mock/MockServiceRegistry.h"
#include "service/network/INetworkConfig.h"

namespace cosmo::test {

TEST_CASE("MessageNetworkHandler: QueryNetCard", "[network-handler]") {
    MockServiceRegistry mocks;
    MessageNetworkHandler handler(mocks.networkSvc);

    SECTION("returns two cards with correct DTO mapping") {
        platform::NetCardInfo main_card;
        main_card.is_main  = true;
        main_card.dhcp     = 1;
        main_card.ip_addr  = "192.168.1.100";
        main_card.net_mask = "255.255.255.0";
        main_card.gateway  = "192.168.1.1";
        main_card.mac      = "AA:BB:CC:DD:EE:FF";

        platform::NetCardInfo sub_card;
        sub_card.is_main  = false;
        sub_card.dhcp     = 0;
        sub_card.ip_addr  = "10.0.0.5";
        sub_card.net_mask = "255.0.0.0";
        sub_card.gateway  = "10.0.0.1";
        sub_card.mac      = "11:22:33:44:55:66";

        REQUIRE_CALL(mocks.networkSvc, GetCardRealInfos())
            .RETURN(std::vector<platform::NetCardInfo>{main_card, sub_card});

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(Network::MsgQueryNetCardRecv{}, errc);

        REQUIRE(result.resData.netCardList.size() == 2);
        auto& first = result.resData.netCardList[0];
        CHECK(first.mainCard == 1);
        CHECK(first.dhcp == 1);
        CHECK(first.ethName == std::string(platform::kNetworkMainCardName));
        CHECK(first.ipAddr == "192.168.1.100");
        CHECK(first.mac == "AA:BB:CC:DD:EE:FF");

        auto& second = result.resData.netCardList[1];
        CHECK(second.mainCard == 0);
        CHECK(second.ethName == std::string(platform::kNetworkSubCardName));
    }
}

TEST_CASE("MessageNetworkHandler: ModifyNetCard", "[network-handler]") {
    MockServiceRegistry mocks;
    MessageNetworkHandler handler(mocks.networkSvc);

    SECTION("forwards card info to ApplyCardInfoAsync") {
        Network::MsgModifyNetCardRecv recv{};
        recv.netCard.mainCard = 1;
        recv.netCard.dhcp     = 0;
        recv.netCard.ipAddr   = "192.168.1.50";
        recv.netCard.netMask  = "255.255.255.0";
        recv.netCard.gateway  = "192.168.1.1";

        REQUIRE_CALL(mocks.networkSvc, ApplyCardInfoAsync(trompeloeil::_))
            .WITH(_1.is_main == true)
            .WITH(_1.dhcp == 0)
            .WITH(_1.ip_addr == "192.168.1.50")
            .WITH(_1.eth_name == std::string(platform::kNetworkMainEthName));

        std::error_condition errc = util::ErrorEnum::Success;
        handler.Handle(std::move(recv), errc);
    }
}

TEST_CASE("MessageNetworkHandler: QueryNetDns", "[network-handler]") {
    MockServiceRegistry mocks;
    MessageNetworkHandler handler(mocks.networkSvc);

    SECTION("returns empty when no DNS configured") {
        REQUIRE_CALL(mocks.networkSvc, GetCfgDns()).RETURN(std::vector<std::string>{});

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(Network::MsgQueryNetDnsRecv{}, errc);

        CHECK(result.resData.dns1.empty());
        CHECK(result.resData.dns2.empty());
    }

    SECTION("returns one DNS") {
        REQUIRE_CALL(mocks.networkSvc, GetCfgDns()).RETURN(std::vector<std::string>{"8.8.8.8"});

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(Network::MsgQueryNetDnsRecv{}, errc);

        CHECK(result.resData.dns1 == "8.8.8.8");
        CHECK(result.resData.dns2.empty());
    }

    SECTION("returns two DNS") {
        REQUIRE_CALL(mocks.networkSvc, GetCfgDns()).RETURN((std::vector<std::string>{"8.8.8.8", "8.8.4.4"}));

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(Network::MsgQueryNetDnsRecv{}, errc);

        CHECK(result.resData.dns1 == "8.8.8.8");
        CHECK(result.resData.dns2 == "8.8.4.4");
    }
}

TEST_CASE("MessageNetworkHandler: ModifyNetDns", "[network-handler]") {
    MockServiceRegistry mocks;
    MessageNetworkHandler handler(mocks.networkSvc);

    SECTION("sets DNS successfully") {
        REQUIRE_CALL(mocks.networkSvc, SetDnss(trompeloeil::_)).WITH(_1.size() == 2).RETURN(true);

        Network::MsgModifyNetDnsRecv recv{};
        recv.dns1 = "8.8.8.8";
        recv.dns2 = "8.8.4.4";

        std::error_condition errc = util::ErrorEnum::Success;
        handler.Handle(std::move(recv), errc);

        CHECK(errc == util::ErrorEnum::Success);
    }

    SECTION("reports failure when SetDnss returns false") {
        REQUIRE_CALL(mocks.networkSvc, SetDnss(trompeloeil::_)).RETURN(false);

        Network::MsgModifyNetDnsRecv recv{};
        recv.dns1 = "invalid";

        std::error_condition errc = util::ErrorEnum::Success;
        handler.Handle(std::move(recv), errc);

        CHECK(errc == util::ErrorEnum::Failed);
    }
}

TEST_CASE("MessageNetworkHandler: NetworkQualityCheck", "[network-handler]") {
    MockServiceRegistry mocks;
    MessageNetworkHandler handler(mocks.networkSvc);

    SECTION("returns probe result on success") {
        service::INetworkConfig::PingQualityResult probe_result;
        probe_result.lost_rate       = 25.0f;
        probe_result.average_latency = 10.5f;
        probe_result.success         = true;

        REQUIRE_CALL(mocks.networkSvc, ProbeNetworkQuality("192.168.1.1", 1000)).RETURN(probe_result);

        Network::MsgNetworkQualityCheckRecv recv{};
        recv.ip         = "192.168.1.1";
        recv.packetSize = 1000;

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(std::move(recv), errc);

        CHECK(result.resData.lostRate == Catch::Approx(25.0f));
        CHECK(result.resData.averageLatency == Catch::Approx(10.5f));
        CHECK(result.resCode == 0);
    }

    SECTION("returns error code on probe failure") {
        service::INetworkConfig::PingQualityResult probe_result;
        probe_result.success = false;

        REQUIRE_CALL(mocks.networkSvc, ProbeNetworkQuality(trompeloeil::_, trompeloeil::_))
            .RETURN(probe_result);

        Network::MsgNetworkQualityCheckRecv recv{};
        recv.ip         = "10.0.0.1";
        recv.packetSize = 64;

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(std::move(recv), errc);

        CHECK(result.resCode == static_cast<int>(util::ErrorEnum::IpProbeFailed));
    }
}

TEST_CASE("MessageNetworkHandler: IpAccessibleCheck", "[network-handler]") {
    MockServiceRegistry mocks;
    MessageNetworkHandler handler(mocks.networkSvc);

    SECTION("returns accessible=1 when reachable") {
        REQUIRE_CALL(mocks.networkSvc, IsIpAccessible("192.168.1.1")).RETURN(true);

        Network::MsgIpAccessibleCheckRecv recv{};
        recv.ip = "192.168.1.1";

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(std::move(recv), errc);

        CHECK(result.resData.accessible == 1);
        CHECK(result.resData.ip == "192.168.1.1");
        CHECK(result.resCode == 0);
    }

    SECTION("returns error code when unreachable") {
        REQUIRE_CALL(mocks.networkSvc, IsIpAccessible("10.0.0.99")).RETURN(false);

        Network::MsgIpAccessibleCheckRecv recv{};
        recv.ip = "10.0.0.99";

        std::error_condition errc = util::ErrorEnum::Success;
        auto result               = handler.Handle(std::move(recv), errc);

        CHECK(result.resData.accessible == 0);
        CHECK(result.resCode == static_cast<int>(util::ErrorEnum::IpProbeFailed));
    }
}

}  // namespace cosmo::test
