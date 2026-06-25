#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/event/IAlarmPushService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockAlarmPushService : public cosmo::service::IAlarmPushService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK0(IsEnabled, bool(), override);
    MAKE_MOCK0(GetUrl, std::string(), override);
    MAKE_MOCK2(SetPush, cosmo::util::ErrorEnum(bool, const std::string&), override);
};

}  // namespace cosmo::test
