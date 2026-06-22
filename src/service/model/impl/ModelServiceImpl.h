// ModelService implementation — orchestrator that delegates to extracted helpers.
// Contains model CRUD, config management, and path access.
#pragma once

#include <map>
#include <shared_mutex>

#include "nlohmann/json_fwd.hpp"
#include "service/model/IModelService.h"
#include "service/model/impl/ModelImportExporter.h"
#include "service/model/impl/ModelPathMapper.h"
#include "service/model/impl/ModelUploadHelper.h"

namespace cosmo::service {

class ModelServiceImpl : public IModelService {
public:
    ModelServiceImpl() = default;

    void Init() override;

    // ---- Path access ----
    std::string GetModelPath() override;
    std::string GetModelTemplatePath() override;
    std::string GetModelComponentsJsonPath() override;

    // ---- Model upload ----
    cosmo::util::ErrorEnum ModelAdd(const std::string& filePath) override;

    // ---- Model query (disk-based) ----
    bool ModelValid(const std::string& modelCode) override;
    bool ModelValid(const std::string& modelCode, std::string& modelName) override;
    std::vector<cosmo::ModelInfo> QueryModelInfo(const std::string& modelName, const std::string& modelCode,
                                                 int pageNum, int pageSize, size_t& total) override;
    cosmo::ModelInfo GetModelInfo(const std::string& modelCode) override;

    // ---- Temp file upload (delegates to ModelUploadHelper) ----
    cosmo::util::ErrorEnum UploadTempFile(const std::string& filePath, const std::string& fileName,
                                          const std::string& contentLength, const std::string& uploadId,
                                          const std::string& chunkIndex, const std::string& totalChunks,
                                          std::string& persistentPath) override;

    // ---- Config CRUD ----
    cosmo::util::ErrorEnum GetModelConfig(const std::string& modelCode, std::string& configJson) override;
    cosmo::util::ErrorEnum SaveModelConfig(const std::string& modelCode,
                                           const std::string& configJson) override;

    // ---- Components ----
    std::vector<cosmo::Model::MsgModelComponent> GetModelComponents() override;

    // ---- Model CRUD ----
    cosmo::util::ErrorEnum DeleteModel(const std::string& modelCode) override;
    cosmo::util::ErrorEnum UpdateModel(const std::string& modelCode, const std::string& modelName,
                                       int maxBatch, const std::string& description) override;

    // ---- Query ----
    void QueryModels(const std::string& modelName, const std::string& modelCode, int pageNum, int pageSize,
                     int& total, std::vector<cosmo::Model::MsgModel>& rows) override;
    std::vector<cosmo::Model::MsgAtomicModel> QueryAtomicModels(const std::string& modelName,
                                                                const std::string& modelType,
                                                                const std::string& filePath) override;

    // ---- Export/Import (delegates to ModelImportExporter) ----
    cosmo::util::ErrorEnum ExportModelConfig(const std::string& modelCode, const std::string& modelName,
                                             std::string& filePath, std::string& fileName) override;
    cosmo::util::ErrorEnum ImportModel(const std::string& archivePath) override;
    cosmo::util::ErrorEnum AddAtomicModel(const std::string& modelCode, const std::string& modelName,
                                          const std::string& modelType, const std::string& description,
                                          const std::vector<cosmo::Model::BmodelFileInfo>& bmodelFiles,
                                          const std::string& vocabFilePath,
                                          const std::string& tokenizerFilePath,
                                          const std::string& normalizationMode,
                                          const std::string& colorChannel) override;

    // ---- Model path mapping (delegates to ModelPathMapper) ----
    void SetModelPathMapping(const std::string& algCode, const std::string& modelPath) override;
    std::string GetModelPathMapping(const std::string& algCode) override;
    bool GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath) override;
    bool GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath,
                     std::string& wordDictPath) override;

private:
    // Shared helpers (also used by ModelImportExporter via lambda injection)
    std::string FindModelDir(const std::string& modelCode);
    std::vector<std::string> GetModelSearchPaths();
    std::string GenerateUniqueModelCode();
    void ValidateModelOutputFormat(const nlohmann::json& doc);
    void NotifyAlgorithmsChanged(const std::string& modelCode, bool restartRunning);

    // ---- ModelMng Private Structs ----
    struct ModelJsonOutputInfo;
    struct ModelJsonInstruction;
    struct ModelLabel;
    struct ModelJsonConfig;
    struct ModelJsonInfo;
    struct ModelLabelInfo;

    std::string UpzipModelFile(std::string filePath);
    cosmo::util::ErrorEnum CheckModelValid(std::string unZipFile, ModelJsonInfo& cfgInfo);

    // ---- State ----
    std::shared_mutex mtx_;

    // ---- Extracted sub-components ----
    ModelPathMapper path_mapper_;
    ModelImportExporter import_exporter_;
    ModelUploadHelper upload_helper_;
};

}  // namespace cosmo::service
