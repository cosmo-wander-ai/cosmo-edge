#pragma once
// clang-format off
#include "catch_amalgamated.hpp"
#include "catch2/trompeloeil.hpp"
// clang-format on
#include "flow/common/AlgDataUnit.h"
#include "service/model/IModelService.h"
#include "trompeloeil.hpp"
#include "util/PathUtil.h"

namespace cosmo::test {

class MockModelService : public cosmo::service::IModelService {
public:
    MAKE_MOCK0(Init, void(), override);
    MAKE_MOCK0(GetModelPath, std::string(), override);
    MAKE_MOCK0(GetModelTemplatePath, std::string(), override);
    MAKE_MOCK0(GetModelComponentsJsonPath, std::string(), override);
    MAKE_MOCK1(ModelAdd, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK1(ModelValid, bool(const std::string&), override);
    MAKE_MOCK2(ModelValid, bool(const std::string&, std::string&), override);
    MAKE_MOCK5(QueryModelInfo,
               std::vector<cosmo::ModelInfo>(const std::string&, const std::string&, int, int, size_t&),
               override);
    MAKE_MOCK1(GetModelInfo, cosmo::ModelInfo(const std::string&), override);
    MAKE_MOCK7(UploadTempFile,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::string&, const std::string&,
                                      std::string&),
               override);
    MAKE_MOCK2(GetModelConfig, cosmo::util::ErrorEnum(const std::string&, std::string&), override);
    MAKE_MOCK2(SaveModelConfig, cosmo::util::ErrorEnum(const std::string&, const std::string&), override);
    MAKE_MOCK0(GetModelComponents, std::vector<cosmo::Model::MsgModelComponent>(), override);
    MAKE_MOCK1(DeleteModel, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK4(UpdateModel,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, int, const std::string&),
               override);
    MAKE_MOCK6(QueryModels,
               void(const std::string&, const std::string&, int, int, int&,
                    std::vector<cosmo::Model::MsgModel>&),
               override);
    MAKE_MOCK3(QueryAtomicModels,
               std::vector<cosmo::Model::MsgAtomicModel>(const std::string&, const std::string&,
                                                         const std::string&),
               override);
    MAKE_MOCK4(ExportModelConfig,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, std::string&, std::string&),
               override);
    MAKE_MOCK1(ImportModel, cosmo::util::ErrorEnum(const std::string&), override);
    MAKE_MOCK9(AddAtomicModel,
               cosmo::util::ErrorEnum(const std::string&, const std::string&, const std::string&,
                                      const std::string&, const std::vector<cosmo::Model::BmodelFileInfo>&,
                                      const std::string&, const std::string&, const std::string&,
                                      const std::string&),
               override);
    MAKE_MOCK2(SetModelPathMapping, void(const std::string&, const std::string&), override);
    MAKE_MOCK1(GetModelPathMapping, std::string(const std::string&), override);
    MAKE_MOCK3(GetModelCfg, bool(const std::string&, std::string&, std::string&), override);
    MAKE_MOCK4(GetModelCfg, bool(const std::string&, std::string&, std::string&, std::string&), override);
};

}  // namespace cosmo::test
