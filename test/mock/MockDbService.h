#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/infra/IDbService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockDbService : public cosmo::service::IDbService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK0(GetDb, std::shared_ptr<SQLite::Database>(), override);
};

}  // namespace cosmo::test
