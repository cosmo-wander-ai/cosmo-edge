#include <cerrno>
#include <nlohmann/json.hpp>
#include <vector>

#include "catch_amalgamated.hpp"
#include "mock/MockNetworkService.h"
#include "service/network/DeviceDiscoveryTypes.h"
#include "service/network/impl/DeviceDiscoveryReceivePolicy.h"
#include "service/network/impl/DeviceDiscoveryServiceImpl.h"
#include "support/ScopedServiceOverride.h"

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

    SECTION("hardware write response") {
        cosmo::service::HWInfoWriteResponse response;
        response.cmd     = "writeHWInfo";
        response.type    = "ack";
        response.reqId   = "hardware-request";
        response.resCode = 0;
        response.resMsg  = "Success";

        nlohmann::json encoded = response;
        auto decoded           = encoded.get<cosmo::service::HWInfoWriteResponse>();

        REQUIRE(encoded.at("cmd") == "writeHWInfo");
        REQUIRE(encoded.at("type") == "ack");
        REQUIRE(encoded.at("reqId") == "hardware-request");
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
    cosmo::test::MockNetworkService networkSvc;
    cosmo::test::ScopedServiceOverride<cosmo::service::INetworkService> registration(networkSvc);
    // Some test hosts cannot join multicast through INADDR_ANY. The service
    // then asks the network service for a main-interface fallback; keep that
    // environment-dependent branch inside the mock contract.
    ALLOW_CALL(networkSvc, GetCardRealInfos()).RETURN(std::vector<cosmo::platform::NetCardInfo>{});

    cosmo::service::DeviceDiscoveryServiceImpl sut("239.255.0.0", 46000);
    REQUIRE_NOTHROW(sut.Start());
    REQUIRE_NOTHROW(sut.Stop());
}
