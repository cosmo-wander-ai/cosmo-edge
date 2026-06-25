#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/infra/ILinkageService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockLinkageService : public cosmo::service::ILinkageService {
public:
    MAKE_MOCK3(Add, cosmo::util::ErrorEnum(const std::string&, const std::string&, std::string&), override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(std::string&), override);
    MAKE_MOCK3(Update, cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&),
               override);
    MAKE_MOCK4(Query, std::vector<cosmo::LinkageStrategyOutputUnit>(int, int, const std::string&, size_t&),
               override);
    MAKE_MOCK2(Switch, cosmo::util::ErrorEnum(std::string&, bool), override);
    MAKE_MOCK2(ReadSupportedStorage, bool(int&, std::vector<cosmo::StorageList>&), override);
    MAKE_MOCK1(IsAudioDeviceInUse, bool(const std::string&), override);
    MAKE_MOCK1(IsAudioFileInUse, bool(const std::string&), override);
    MAKE_MOCK2(Alarm, bool(const std::string&, const std::string&), override);
};

}  // namespace cosmo::test
