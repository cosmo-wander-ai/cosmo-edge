#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/system/IConfigReadService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockConfigReadService : public cosmo::service::IConfigReadService {
public:
    MAKE_MOCK0(GetPictureQuality, cosmo::CfgAlarmParamOverviewInfo(), override);
    MAKE_MOCK0(GetAlarmVideoDuration, cosmo::CfgAlarmParamVideoRecordInfo(), override);
    MAKE_MOCK0(GetRebootParam, cosmo::CfgRebootParamInfo(), override);
    MAKE_MOCK0(GetSystemLogo, cosmo::service::SystemLogoInfo(), override);
    MAKE_MOCK0(GetDebugMode, bool(), override);
    MAKE_MOCK0(GetShieldedActions, std::vector<std::string>(), override);
    MAKE_MOCK1(GetActionSwitch, bool(const std::string&), override);
    MAKE_MOCK3(GetPopUpParam, void(int&, int&, int&), override);
    MAKE_MOCK0(GetRunMode, cosmo::RunMode(), override);
    MAKE_MOCK0(GetResourceLimit, bool(), override);
    MAKE_MOCK0(IsNetworkModel, bool(), override);
};

}  // namespace cosmo::test
