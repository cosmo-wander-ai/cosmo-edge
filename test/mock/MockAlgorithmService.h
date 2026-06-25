#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/algorithm/IAlgorithmService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockAlgorithmService : public cosmo::service::IAlgorithmService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK1(Add, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK1(Add, cosmo::util::ErrorEnum(const cosmo::service::algorithm::AlgorithmPacketInfo&), override);
    MAKE_MOCK6(AddFromJson,
               cosmo::util::ErrorEnum(const std::string&, int, int, const std::string&, const std::string&,
                                      const std::string&),
               override);
    MAKE_MOCK1(LayoutSave, cosmo::util::ErrorEnum(const cosmo::service::algorithm::LayoutSaveReq&), override);
    MAKE_MOCK3(GetLayoutDetail,
               cosmo::util::ErrorEnum(const std::string&, const std::string&,
                                      cosmo::service::algorithm::LayoutDetailResult&),
               override);
    MAKE_MOCK4(GetLayoutList,
               cosmo::util::ErrorEnum(const std::string&, int, const std::string&,
                                      cosmo::service::algorithm::LayoutListResult&),
               override);
    MAKE_MOCK6(LayoutExportSingle,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::string&,
                                      cosmo::service::algorithm::LayoutExportResult&),
               override);
    MAKE_MOCK6(LayoutExportAll,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::vector<std::string>&,
                                      cosmo::service::algorithm::LayoutExportResult&),
               override);
    MAKE_MOCK3(GetAtomicActionList,
               cosmo::util::ErrorEnum(int, const std::string&,
                                      cosmo::service::algorithm::AtomicActionListResult&),
               override);
    MAKE_MOCK1(Delete, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK4(Update,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, int, const std::string&),
               override);
    MAKE_MOCK8(Query,
               std::vector<cosmo::service::algorithm::AlgorithmPacketInfo>(
                   const std::string&, const std::string&, const std::string&, const std::string&,
                   const std::string&, int, int, size_t&),
               override);
    MAKE_MOCK1(GetAlgorithm, cosmo::ActionAlgPtr(const std::string&), override);
    MAKE_MOCK1(GetAlgorithmName, std::string(const std::string&), override);
    MAKE_MOCK1(GetMetaData, std::string(const std::string&), override);
    MAKE_MOCK1(GetAlgorithmsByModelId, std::vector<std::string>(const std::string&), override);
    MAKE_MOCK1(ReloadAlgorithmFromFile, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK0(GetAlgorithmPath, std::string(), override);
    MAKE_MOCK0(GetActionsJsonPath, std::string(), override);
    MAKE_MOCK0(GetPassFlowAlgorithms, std::vector<cosmo::service::algorithm::AlgorithmLocalInfo>(), override);
};

}  // namespace cosmo::test
