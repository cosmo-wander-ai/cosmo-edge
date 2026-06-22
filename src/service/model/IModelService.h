/// @file IModelService.h
/// @brief Model service interface — full CRUD, import/export, chunked upload,
///        and configuration management for AI model packages.
///        Inherits IModelQuery and IModelPathMapping for ISP compliance.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/model/IModelPathMapping.h"
#include "service/model/IModelQuery.h"
#include "service/model/dto/ModelDto.h"
#include "util/ErrorCode.h"

namespace cosmo {
struct ModelLabel {
    std::string code;
    std::string labelName;
    std::string label;
    float confidenceHigh{-1.0};
    float confidence{-1.0};
    friend void to_json(nlohmann::json& j, const ModelLabel& v);
    friend void from_json(const nlohmann::json& j, ModelLabel& v);
};

struct ModelInfo {
    std::string id;
    std::string name;
    std::string version;
    int64_t timestamp{0};
    std::string path;
    std::vector<ModelLabel> labels;
    friend void to_json(nlohmann::json& j, const ModelInfo& v);
    friend void from_json(const nlohmann::json& j, ModelInfo& v);
};
}  // namespace cosmo

namespace cosmo::service {

/// Aggregate model management service providing full lifecycle operations
/// for AI model packages (installation, configuration, export, import).
///
/// Inherits narrow query (IModelQuery) and path mapping (IModelPathMapping)
/// sub-interfaces.  Consumers that only need read access should prefer
/// those narrow interfaces.
class IModelService : public IModelQuery, public IModelPathMapping {
public:
    virtual ~IModelService() = default;

    /// Post-registration initialization. Loads model metadata from disk.
    virtual void Init() = 0;

    // ── Path Access ──

    /// Get the base directory for model storage.
    virtual std::string GetModelPath() = 0;

    /// Get the path to model template definitions.
    virtual std::string GetModelTemplatePath() = 0;

    /// Get the path to the model components JSON file.
    virtual std::string GetModelComponentsJsonPath() = 0;

    // ── Legacy Model Upload ──

    /// Add a model from a legacy archive file.
    /// @param filePath Path to the model archive.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ModelAdd(const std::string& filePath) = 0;

    // ── Chunked File Upload ──

    /// Upload a temporary file chunk (supports resumable uploads).
    /// @param filePath       Local path of the uploaded chunk.
    /// @param fileName       Original file name.
    /// @param contentLength  Total content length.
    /// @param uploadId       Unique upload session ID.
    /// @param chunkIndex     Current chunk index (0-based).
    /// @param totalChunks    Total number of chunks.
    /// @param persistentPath [out] Path to the assembled file after all chunks arrive.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum UploadTempFile(const std::string& filePath, const std::string& fileName,
                                                  const std::string& contentLength,
                                                  const std::string& uploadId, const std::string& chunkIndex,
                                                  const std::string& totalChunks,
                                                  std::string& persistentPath) = 0;

    // ── Model Configuration ──

    /// Get the JSON configuration of a model.
    /// @param modelCode  Model code identifier.
    /// @param configJson [out] Configuration JSON string.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum GetModelConfig(const std::string& modelCode, std::string& configJson) = 0;

    /// Save a model's JSON configuration.
    /// @param modelCode  Model code identifier.
    /// @param configJson Configuration JSON string to save.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum SaveModelConfig(const std::string& modelCode,
                                                   const std::string& configJson) = 0;

    // ── Model CRUD ──

    /// Delete a model by code.
    /// @param modelCode Model code identifier.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum DeleteModel(const std::string& modelCode) = 0;

    /// Update model metadata.
    /// @param modelCode   Model code identifier.
    /// @param modelName   Updated display name.
    /// @param maxBatch    Updated maximum batch size.
    /// @param description Updated description text.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum UpdateModel(const std::string& modelCode, const std::string& modelName,
                                               int maxBatch, const std::string& description) = 0;

    // ── Export ──

    /// Export a model as a portable package.
    /// @param modelCode Model code identifier.
    /// @param modelName Model display name (used in the export filename).
    /// @param filePath  [out] Path to the exported file.
    /// @param fileName  [out] Export file name.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ExportModelConfig(const std::string& modelCode,
                                                     const std::string& modelName, std::string& filePath,
                                                     std::string& fileName) = 0;

    // ── Import ──

    /// Import a model from an archive file.
    /// @param archivePath Path to the model archive.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ImportModel(const std::string& archivePath) = 0;

    // ── Atomic Model Addition ──

    /// Add a new atomic model with full metadata.
    /// @param modelCode          Unique model code.
    /// @param modelName          Display name.
    /// @param modelType          Model type (detection, recognition, etc.).
    /// @param description        Description text.
    /// @param bmodelFiles        List of .bmodel file descriptors.
    /// @param vocabFilePath      Path to vocabulary file (for NLP models).
    /// @param tokenizerFilePath  Path to tokenizer file (for NLP models).
    /// @param normalizationMode  Input normalization mode.
    /// @param colorChannel       Input color channel order (BGR/RGB).
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum AddAtomicModel(
        const std::string& modelCode, const std::string& modelName, const std::string& modelType,
        const std::string& description, const std::vector<cosmo::Model::BmodelFileInfo>& bmodelFiles,
        const std::string& vocabFilePath, const std::string& tokenizerFilePath,
        const std::string& normalizationMode, const std::string& colorChannel) = 0;
};

}  // namespace cosmo::service
