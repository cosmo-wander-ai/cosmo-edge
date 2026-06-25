#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/network/IDeviceDiscoveryService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockDeviceDiscoveryService : public cosmo::service::IDeviceDiscoveryService {
public:
    MAKE_MOCK0(Start, void(), override);
    MAKE_MOCK0(Stop, void(), override);
};

}  // namespace cosmo::test
