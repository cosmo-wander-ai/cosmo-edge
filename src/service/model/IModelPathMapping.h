/// @file IModelPathMapping.h
/// @brief Model path mapping interface — resolves algorithm codes to model
///        binary and configuration file paths.
///        ISP split from IModelService.
///        Consumed by flow/infer layers for model config resolution.
#pragma once

#include <string>

#include "service/detail/ServiceRegistry.h"

namespace cosmo::service {

/// Maintains a runtime mapping from algorithm codes to model file paths
/// and configuration files.
///
/// Used by the inference layer to locate .bmodel binaries and their
/// associated config.json files when initializing NPU models.
class IModelPathMapping {
public:
    virtual ~IModelPathMapping() = default;

    /// Register a model path for an algorithm code.
    /// @param algCode   Algorithm code identifier.
    /// @param modelPath Absolute path to the model directory.
    virtual void SetModelPathMapping(const std::string& algCode, const std::string& modelPath) = 0;

    /// Get the registered model path for an algorithm code.
    /// @param algCode Algorithm code identifier.
    /// @return Model directory path, or empty if not mapped.
    virtual std::string GetModelPathMapping(const std::string& algCode) = 0;

    /// Get model configuration and binary paths for an algorithm code.
    /// @param algCode   Algorithm code identifier.
    /// @param cfgPath   [out] Path to the config.json file.
    /// @param modelPath [out] Path to the .bmodel binary.
    /// @return true if the mapping exists and paths were resolved.
    virtual bool GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath) = 0;

    /// Get model configuration, binary, and word dictionary paths.
    /// @param algCode      Algorithm code identifier.
    /// @param cfgPath      [out] Path to the config.json file.
    /// @param modelPath    [out] Path to the .bmodel binary.
    /// @param wordDictPath [out] Path to the word dictionary file (for NLP models).
    /// @return true if the mapping exists and paths were resolved.
    virtual bool GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath,
                             std::string& wordDictPath) = 0;
};

// Narrow-interface accessor — delegates to ServiceRegistry internally.

}  // namespace cosmo::service
