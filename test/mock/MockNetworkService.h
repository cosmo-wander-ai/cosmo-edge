#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/network/INetworkService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockNetworkService : public cosmo::service::INetworkService {
public:
    MAKE_MOCK0(IsMqttRegistered, bool(), override);
    MAKE_MOCK0(IsMqttEnabled, bool(), override);
    MAKE_MOCK0(MqttStop, void(), override);
    MAKE_MOCK0(MqttStart, void(), override);
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK1(GetCardRealInfo, cosmo::platform::NetCardInfo(bool), override);
    MAKE_MOCK0(GetCardRealInfos, std::vector<cosmo::platform::NetCardInfo>(), override);
    MAKE_MOCK1(SetCardInfo, bool(const cosmo::platform::NetCardInfo&), override);
    MAKE_MOCK1(ApplyCardInfoAsync, void(const cosmo::platform::NetCardInfo&), override);
    MAKE_MOCK0(GetCfgDns, std::vector<std::string>(), override);
    MAKE_MOCK1(SetDnss, bool(std::vector<std::string>), override);
    MAKE_MOCK3(SearchSetNewInfo, bool(cosmo::platform::NetCardInfo&, const std::string&, const std::string&),
               override);
    MAKE_MOCK0(GetNetCards, std::vector<cosmo::service::NetCardView>(), override);
    MAKE_MOCK0(GetHostIpAddress, std::string(), override);
    MAKE_MOCK2(InitHttpServer, void(const std::string&, uint16_t), override);
    MAKE_MOCK0(RunHttpLoop, void(), override);
    MAKE_MOCK0(StopHttpServer, void(), override);
    MAKE_MOCK2(ProbeNetworkQuality,
               cosmo::service::INetworkConfig::PingQualityResult(const std::string&, int), override);
    MAKE_MOCK1(IsIpAccessible, bool(const std::string&), override);
};

}  // namespace cosmo::test
