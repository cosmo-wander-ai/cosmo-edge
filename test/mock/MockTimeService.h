#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/system/ITimeService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockTimeService : public cosmo::service::ITimeService {
public:
    MAKE_MOCK1(GetTimeStatus, cosmo::service::TimeStatus(std::vector<cosmo::service::TimeZoneItem>&),
               override);
    MAKE_MOCK2(SyncNtp, cosmo::util::ErrorEnum(const cosmo::service::NtpConfig&, int), override);
    MAKE_MOCK2(SetTime, cosmo::util::ErrorEnum(int64_t, int), override);
};

}  // namespace cosmo::test
