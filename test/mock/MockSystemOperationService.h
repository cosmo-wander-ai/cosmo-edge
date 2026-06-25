#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/system/ISystemOperationService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockSystemOperationService : public cosmo::service::ISystemOperationService {
public:
    MAKE_MOCK1(RebootDevice, void(const std::string&), override);
    MAKE_MOCK1(ResetDevice, void(const std::string&), override);
    MAKE_MOCK2(ExportLogs, cosmo::util::ErrorEnum(std::string&, std::string&), override);
    MAKE_MOCK1(Upgrade, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK0(ShowThreadDebugInfo, void(), override);
};

}  // namespace cosmo::test
