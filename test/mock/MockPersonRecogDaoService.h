#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/face/IPersonRecogDaoService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockPersonRecogDaoService : public cosmo::service::IPersonRecogDaoService {
public:
    MAKE_MOCK0(Begin, void(), override);
    MAKE_MOCK0(Commit, void(), override);
    MAKE_MOCK0(Rollback, void(), override);
    MAKE_MOCK1(AddPersonLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(UpdatePersonLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(RemovePersonLib, bool(const std::string&), override);
    MAKE_MOCK1(ClearPersonLib, bool(const std::string&), override);
    MAKE_MOCK0(GetAllPersonLibs, std::vector<std::string>(), override);
    MAKE_MOCK4(AddPerson,
               bool(const std::string&, const std::string&, const std::string&, const std::vector<float>&),
               override);
    MAKE_MOCK1(RemovePerson, bool(const std::string&), override);
    MAKE_MOCK1(QueryPersonLib,
               cosmo::db::PersonRecogLibQueryResult(const cosmo::db::PersonRecogLibQueryCondition&),
               override);
    MAKE_MOCK1(QueryPersons, cosmo::db::PersonRecogQueryResult(const cosmo::db::PersonRecogQueryCondition&),
               override);
};

}  // namespace cosmo::test
