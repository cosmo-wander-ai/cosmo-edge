#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/task/IScheduleService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockScheduleService : public cosmo::service::IScheduleService {
public:
    MAKE_MOCK2(Add, cosmo::util::ErrorEnum(cosmo::MsgScheduleTemplate&, std::string&), override);
    MAKE_MOCK1(Update, cosmo::util::ErrorEnum(cosmo::MsgScheduleTemplate&), override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK4(Query, std::vector<cosmo::MsgScheduleTemplate>(const std::string&, int, int, size_t&),
               override);
    MAKE_MOCK2(Exist2, bool(const std::string&, std::string&));
    bool Exist(const std::string& id, std::string& name) override {
        return Exist2(id, name);
    }
    MAKE_MOCK1(Exist1, bool(const std::string&));
    bool Exist(const std::string& id) override {
        return Exist1(id);
    }
    MAKE_MOCK1(InRunTime, bool(const std::string&), override);
    MAKE_MOCK0(GetDefaultId, std::string(), override);
};

}  // namespace cosmo::test
