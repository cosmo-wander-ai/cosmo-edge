#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/system/IConfigWriteService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockConfigWriteService : public cosmo::service::IConfigWriteService {
public:
    MAKE_MOCK1(SetPictureQuality, cosmo::util::ErrorEnum(cosmo::CfgAlarmParamOverviewInfo), override);
    MAKE_MOCK0(ResetPictureQuality, cosmo::util::ErrorEnum(), override);
    MAKE_MOCK1(SetAlarmVideoDuration, cosmo::util::ErrorEnum(cosmo::CfgAlarmParamVideoRecordInfo), override);
    MAKE_MOCK0(ResetAlarmVideoDuration, cosmo::util::ErrorEnum(), override);
    MAKE_MOCK1(SetRebootParam, cosmo::util::ErrorEnum(cosmo::CfgRebootParamInfo), override);
    MAKE_MOCK0(ResetRebootParam, cosmo::util::ErrorEnum(), override);
    MAKE_MOCK4(SetSystemLogo,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::vector<uint8_t>&,
                                      const std::string&),
               override);
    MAKE_MOCK1(SetDebugMode, void(bool), override);
    MAKE_MOCK1(SetShieldedActions, void(const std::vector<std::string>&), override);
    MAKE_MOCK3(SetPopUpParam, void(int, int, int), override);
    MAKE_MOCK1(SetRunMode, void(cosmo::RunMode), override);
    MAKE_MOCK1(SetResourceLimit, void(bool), override);
};

}  // namespace cosmo::test
