// ModelImportExporter — handles model import/export and atomic model creation.
// Extracted from ModelServiceImpl to reduce class size.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "infer/BmodelTool.h"
#include "nlohmann/json_fwd.hpp"
#include "service/model/dto/ModelDto.h"
#include "util/ErrorCode.h"

namespace cosmo::service {

class ModelImportExporter {
public:
    ModelImportExporter() = default;

    ModelImportExporter(const ModelImportExporter&)            = delete;
    ModelImportExporter& operator=(const ModelImportExporter&) = delete;

    cosmo::util::ErrorEnum ExportModelConfig(const std::string& modelCode, const std::string& modelName,
                                             std::string& outFilePath, std::string& outFileName);

    cosmo::util::ErrorEnum AddAtomicModel(const std::string& modelCode, const std::string& modelName,
                                          const std::string& modelType, const std::string& description,
                                          const std::vector<cosmo::Model::BmodelFileInfo>& bmodelFiles,
                                          const std::string& vocabFilePath,
                                          const std::string& tokenizerFilePath,
                                          const std::string& normalizationMode,
                                          const std::string& colorChannel);

    cosmo::util::ErrorEnum ImportModel(const std::string& archivePath);

    // Shared helpers needed by these methods (injected from ModelServiceImpl)
    void SetHelpers(std::function<std::string()> getModelPath,
                    std::function<std::string()> getModelTemplatePath,
                    std::function<std::string(const std::string&)> findModelDir,
                    std::function<std::string()> generateUniqueModelCode,
                    std::function<void(const nlohmann::json&)> validateModelOutputFormat,
                    std::function<void(const std::string&, const std::string&)> setModelPathMapping);

private:
    // --- AddAtomicModel helpers (extract method) ---
    cosmo::util::ErrorEnum ValidateAddModelInputs(
        const std::string& modelCode, const std::string& modelName, const std::string& modelType,
        const std::vector<cosmo::Model::BmodelFileInfo>& bmodelFiles, const std::string& vocabFilePath,
        const std::string& tokenizerFilePath, std::string& resolvedModelCode,
        std::vector<std::string>& bmodelPaths);

    cosmo::util::ErrorEnum CollectBmodelInfo(const std::string& modelType,
                                             const std::vector<std::string>& bmodelPaths,
                                             std::vector<cosmo::BmodelInfo>& bmodelInfos,
                                             bool& useTemplateDefaults);

    std::string CalculateNextVersion(const std::string& modelsDir, const std::string& modelCode);

    cosmo::util::ErrorEnum WriteNnFile(const std::string& modelType,
                                       const std::vector<std::string>& bmodelPaths,
                                       const std::string& modelDir);

    void UpdateTemplateConfig(nlohmann::json& templateDoc, const std::string& modelCode,
                              const std::string& versionStr, const std::string& modelName,
                              const std::string& modelType, const std::string& description,
                              const std::vector<cosmo::BmodelInfo>& bmodelInfos, bool useTemplateDefaults,
                              const std::string& normalizationMode, const std::string& colorChannel);

    cosmo::util::ErrorEnum CopyAuxiliaryFiles(const std::string& modelType, const std::string& vocabFilePath,
                                              const std::string& tokenizerFilePath,
                                              const std::string& modelDir);

    // --- ImportModel helpers (extract method) ---
    cosmo::util::ErrorEnum ImportFlatArchive(const std::string& tempDir, const std::string& modelsDir);

    cosmo::util::ErrorEnum ImportDirectoryArchive(const std::string& tempDir, const std::string& modelsDir,
                                                  int& importedCount);

    std::function<std::string()> get_model_path_;
    std::function<std::string()> get_model_template_path_;
    std::function<std::string(const std::string&)> find_model_dir_;
    std::function<std::string()> generate_unique_model_code_;
    std::function<void(const nlohmann::json&)> validate_model_output_format_;
    std::function<void(const std::string&, const std::string&)> set_model_path_mapping_;
};

}  // namespace cosmo::service
