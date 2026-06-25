#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/face/IArticlesReidDaoService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockArticlesReidDaoService : public cosmo::service::IArticlesReidDaoService {
public:
    MAKE_MOCK0(Begin, void(), override);
    MAKE_MOCK0(Commit, void(), override);
    MAKE_MOCK0(Rollback, void(), override);
    MAKE_MOCK1(AddArticlesReidLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(UpdateArticlesReidLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(RemoveArticlesReidLib, bool(const std::string&), override);
    MAKE_MOCK1(ClearArticlesReidLib, bool(const std::string&), override);
    MAKE_MOCK0(GetAllArticlesReidLibs, std::vector<std::string>(), override);
    MAKE_MOCK4(AddArticlesReid,
               bool(const std::string&, const std::string&, const std::string&, const std::vector<float>&),
               override);
    MAKE_MOCK1(RemoveArticlesReid, bool(const std::string&), override);
    MAKE_MOCK1(QueryThingsLib, cosmo::db::ThingsLibQueryResult(const cosmo::db::ThingsLibQueryCondition&),
               override);
    MAKE_MOCK1(QueryThings, cosmo::db::ThingsQueryResult(const cosmo::db::ThingsQueryCondition&), override);
};

}  // namespace cosmo::test
