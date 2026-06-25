#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/network/IAuthService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockAuthService : public cosmo::service::IAuthService {
public:
    MAKE_MOCK2(Login,
               (std::pair<std::string, cosmo::util::ErrorEnum>)(const std::string&, const std::string&),
               override);
    MAKE_MOCK3(ChangePasswd,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&), override);
    MAKE_MOCK1(IsValidToken, bool(const std::string&), override);
};

}  // namespace cosmo::test
