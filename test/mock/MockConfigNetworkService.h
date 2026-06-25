#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/system/IConfigNetworkService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockConfigNetworkService : public cosmo::service::IConfigNetworkService {
public:
    MAKE_MOCK0(GetHttpInterfaceParam, cosmo::service::HttpPushParam(), override);
    MAKE_MOCK1(SetHttpInterfaceParam, cosmo::util::ErrorEnum(const cosmo::service::HttpPushParam&), override);
    MAKE_MOCK0(GetMqttParam, cosmo::service::MqttParam(), override);
    MAKE_MOCK1(SetMqttParam, cosmo::util::ErrorEnum(const cosmo::service::MqttParam&), override);
    MAKE_MOCK0(GetIotNetworkParam, cosmo::service::IotNetworkParam(), override);
    MAKE_MOCK3(SetIotNetworkParam, void(const std::string&, const std::string&, int), override);
};

}  // namespace cosmo::test
