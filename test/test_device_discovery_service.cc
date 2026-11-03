#include <cerrno>
#include <nlohmann/json.hpp>
#include <vector>

#include "catch_amalgamated.hpp"
#include "service/network/DeviceDiscoveryTypes.h"
#include "service/network/impl/DeviceDiscoveryReceivePolicy.h"
#include "service/network/impl/DeviceDiscoveryServiceImpl.h"

TEST_CASE("DeviceDiscoveryService: response envelopes survive JSON round trip", "[device-discovery]") {
    SECTION("probe response") {
        cosmo::service::DiscoveryProbeSend response;
        response.cmd     = "probe";
        response.type    = "ack";
        response.reqId   = "probe-request";
        response.resCode = 0;
        response.resMsg  = "Success";

        nlohmann::json encoded = response;
        auto decoded           = encoded.get<cosmo::service::DiscoveryProbeSend>();

        REQUIRE(encoded.at("cmd") == "probe");
        REQUIRE(encoded.at("type") == "ack");
        REQUIRE(encoded.at("reqId") == "probe-request");
        REQUIRE(decoded.cmd == response.cmd);
        REQUIRE(decoded.type == response.type);
        REQUIRE(decoded.reqId == response.reqId);
    }
}

TEST_CASE("DeviceDiscoveryService: classify receive errors", "[device-discovery]") {
    using cosmo::service::detail::ClassifyMulticastReceiveError;
    using cosmo::service::detail::MulticastReceiveAction;

    REQUIRE(ClassifyMulticastReceiveError(EAGAIN) == MulticastReceiveAction::kRetry);
    REQUIRE(ClassifyMulticastReceiveError(EWOULDBLOCK) == MulticastReceiveAction::kRetry);
    REQUIRE(ClassifyMulticastReceiveError(EINTR) == MulticastReceiveAction::kRetry);
    REQUIRE(ClassifyMulticastReceiveError(EBADF) == MulticastReceiveAction::kRestartSocket);
    REQUIRE(ClassifyMulticastReceiveError(ENETDOWN) == MulticastReceiveAction::kRestartSocket);
}

TEST_CASE("DeviceDiscoveryService: production protocol is probe only", "[device-discovery]") {
    using cosmo::service::detail::IsProductionDiscoveryCommandAllowed;
    using cosmo::service::detail::IsProductionDiscoveryCommandAuthenticationRequired;

    REQUIRE(IsProductionDiscoveryCommandAllowed("probe"));
    REQUIRE_FALSE(IsProductionDiscoveryCommandAuthenticationRequired("probe"));
    REQUIRE_FALSE(IsProductionDiscoveryCommandAllowed("modifyNetCard"));
    REQUIRE_FALSE(IsProductionDiscoveryCommandAllowed("writeHWInfo"));
    REQUIRE_FALSE(IsProductionDiscoveryCommandAllowed("modifyAuthCode"));
    REQUIRE_FALSE(IsProductionDiscoveryCommandAllowed("queryAuthMessage"));
    REQUIRE_FALSE(IsProductionDiscoveryCommandAllowed(""));
    REQUIRE(IsProductionDiscoveryCommandAuthenticationRequired("modifyNetCard"));
    REQUIRE(IsProductionDiscoveryCommandAuthenticationRequired("writeHWInfo"));
    REQUIRE(IsProductionDiscoveryCommandAuthenticationRequired("modifyAuthCode"));
    REQUIRE(IsProductionDiscoveryCommandAuthenticationRequired("queryAuthMessage"));
    REQUIRE_FALSE(IsProductionDiscoveryCommandAuthenticationRequired("unknown"));
}

TEST_CASE("DeviceDiscoveryService: lifecycle safety", "[device-discovery]") {
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
    SECTION("Multicast address and port") {
        REQUIRE_NOTHROW([]() { cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 46000); }());
    }

    SECTION("Different port") {
        REQUIRE_NOTHROW([]() { cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 12345); }());
    }
}

TEST_CASE("DeviceDiscoveryService: Start then Stop", "[device-discovery]") {
    cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 46000);
    REQUIRE_NOTHROW(sut.Start());
    REQUIRE_NOTHROW(sut.Stop());
}

TEST_CASE("DeviceDiscoveryService: memberships follow live addresses on both links", "[device-discovery]") {
    using cosmo::platform::NetworkInterfaceState;
    using cosmo::service::detail::DiscoveryMembershipAddresses;
    NetworkInterfaceState wired;
    wired.interface_name    = "eth0";
    wired.type              = "ethernet";
    wired.link_up           = true;
    wired.ip_addr           = "192.168.0.20";
    wired.is_default        = true;
    auto wireless           = wired;
    wireless.interface_name = "wlP1p1s0";
    wireless.type           = "wifi";
    wireless.ip_addr        = "192.168.0.21";
    wireless.is_default     = false;

    auto memberships = DiscoveryMembershipAddresses({wired, wireless});
    REQUIRE(memberships.size() == 2);
    REQUIRE(memberships[1].first == "wlP1p1s0");
    REQUIRE(memberships[1].second == "192.168.0.21");

    wired.link_up       = false;
    wireless.is_default = true;
    wireless.ip_addr    = "192.168.0.22";
    memberships         = DiscoveryMembershipAddresses({wired, wireless});
    REQUIRE(memberships.size() == 1);
    REQUIRE(memberships[0].second == "192.168.0.22");

    wireless.ip_addr.clear();
    REQUIRE(DiscoveryMembershipAddresses({wired, wireless}).empty());
    wireless.ip_addr = "invalid";
    REQUIRE(DiscoveryMembershipAddresses({wireless}).empty());
    wireless.ip_addr = "127.0.0.1";
    REQUIRE(DiscoveryMembershipAddresses({wireless}).empty());
    wireless.ip_addr = "239.255.0.0";
    REQUIRE(DiscoveryMembershipAddresses({wireless}).empty());
}

TEST_CASE("DeviceDiscoveryService: receive interface cannot be supplied by a packet", "[device-discovery]") {
    const nlohmann::json packet{{"cmd", "probe"},
                                {"type", "req"},
                                {"reqId", "interface-spoof"},
                                {"from", "192.0.2.1"},
                                {"receive_interface_index", 42},
                                {"receive_local_address", 1234}};
    auto request = packet.get<cosmo::service::DiscoveryBaseMsg>();
    REQUIRE(request.from.empty());
    REQUIRE(request.receive_interface_index == 0);
    REQUIRE(request.receive_local_address == 0);
    request.receive_interface_index = 42;
    request.receive_local_address   = 1234;
    const nlohmann::json encoded    = request;
    REQUIRE_FALSE(encoded.contains("receive_interface_index"));
    REQUIRE_FALSE(encoded.contains("receive_local_address"));
}
