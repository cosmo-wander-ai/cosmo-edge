#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/algorithm/IActionService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockActionService : public cosmo::service::IActionService {
public:
    MAKE_MOCK2(GetActionAlg, cosmo::ActionAlgPtr(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetActionAlgByCode, cosmo::ActionAlgPtr(const std::string&), override);
    MAKE_MOCK1(UpdateActionAlg1, bool(std::string&));
    bool UpdateActionAlg(std::string& s) override {
        return UpdateActionAlg1(s);
    }
    MAKE_MOCK1(UpdateActionAlg2, bool(cosmo::ActionAlg&));
    bool UpdateActionAlg(cosmo::ActionAlg& a) override {
        return UpdateActionAlg2(a);
    }
    MAKE_MOCK2(GetPicActionAlg, cosmo::ActionAlgPtr(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetPicActionAlgByCode, cosmo::ActionAlgPtr(const std::string&), override);
    MAKE_MOCK1(UpdatePicActionAlg1, bool(std::string&));
    bool UpdatePicActionAlg(std::string& s) override {
        return UpdatePicActionAlg1(s);
    }
    MAKE_MOCK1(UpdatePicActionAlg2, bool(cosmo::ActionAlg&));
    bool UpdatePicActionAlg(cosmo::ActionAlg& a) override {
        return UpdatePicActionAlg2(a);
    }
};

}  // namespace cosmo::test
