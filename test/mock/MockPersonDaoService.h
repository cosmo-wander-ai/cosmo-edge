#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/face/IPersonDaoService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockPersonDaoService : public cosmo::service::IPersonDaoService {
public:
    MAKE_MOCK0(Begin, void(), override);
    MAKE_MOCK0(Commit, void(), override);
    MAKE_MOCK0(Rollback, void(), override);
    MAKE_MOCK1(AddFaceLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(UpdateFaceLib, bool(const cosmo::db::LibInfo&), override);
    MAKE_MOCK1(RemoveFaceLib, bool(const std::string&), override);
    MAKE_MOCK1(ClearFaceLib, bool(const std::string&), override);
    MAKE_MOCK2(AddPerson,
               bool(const cosmo::db::PersonCondition&,
                    const std::vector<std::pair<std::string, std::vector<float>>>&),
               override);
    MAKE_MOCK1(AddPerson, bool(cosmo::db::FaceRegRecordUnit&), override);
    MAKE_MOCK2(UpdatePerson,
               bool(const cosmo::db::PersonCondition&,
                    const std::vector<std::pair<std::string, std::vector<float>>>&),
               override);
    MAKE_MOCK1(RemovePerson, bool(const std::string&), override);
    MAKE_MOCK1(QueryFaceLib, cosmo::db::FaceLibQueryResult(const cosmo::db::FaceLibQueryCondition&),
               override);
    MAKE_MOCK1(QueryPersons, cosmo::db::FacePersonQueryResult(const cosmo::db::FacePersonQueryCondition&),
               override);
    MAKE_MOCK1(QueryFaceFeature, std::vector<float>(const std::string&), override);
};

}  // namespace cosmo::test
