/// @file IModelQuery.h
/// @brief Model query and validation interface.
///        ISP split from IModelService.
///        Consumed by AlgorithmValidator, CameraServiceImpl, MessageModelHandler.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "service/detail/ServiceRegistry.h"
#include "service/model/dto/ModelDto.h"
#include "util/ErrorCode.h"

// Forward declaration — full definition lives in IModelService.h
namespace cosmo {
struct ModelInfo;
}

namespace cosmo::service {

/// Read-only queries and validation checks for AI model packages.
///
/// Provides model existence validation, metadata queries with pagination,
/// and atomic model component enumeration.
class IModelQuery {
public:
    virtual ~IModelQuery() = default;

    /// Check whether a model is valid (installed and loadable).
    /// @param modelCode Model code identifier.
    /// @return true if the model is valid.
    virtual bool ModelValid(const std::string& modelCode) = 0;

    /// Check whether a model is valid and retrieve its display name.
    /// @param modelCode Model code identifier.
    /// @param modelName [out] Model display name if valid.
    /// @return true if the model is valid.
    virtual bool ModelValid(const std::string& modelCode, std::string& modelName) = 0;

    /// Query model metadata with pagination.
    /// @param modelName Name filter (empty for all).
    /// @param modelCode Code filter (empty for all).
    /// @param pageNum   Page number (1-based).
    /// @param pageSize  Page size.
    /// @param total     [out] Total matching record count.
    /// @return Vector of matching model info structures.
    virtual std::vector<cosmo::ModelInfo> QueryModelInfo(const std::string& modelName,
                                                         const std::string& modelCode, int pageNum,
                                                         int pageSize, size_t& total) = 0;

    /// Get detailed model info by code.
    /// @param modelCode Model code identifier.
    /// @return Model info structure (empty fields if not found).
    virtual cosmo::ModelInfo GetModelInfo(const std::string& modelCode) = 0;

    /// Query models with pagination (DTO output format).
    /// @param modelName Name filter (empty for all).
    /// @param modelCode Code filter (empty for all).
    /// @param pageNum   Page number (1-based).
    /// @param pageSize  Page size.
    /// @param total     [out] Total matching record count.
    /// @param rows      [out] Matching model DTOs.
    virtual void QueryModels(const std::string& modelName, const std::string& modelCode, int pageNum,
                             int pageSize, int& total, std::vector<cosmo::Model::MsgModel>& rows) = 0;

    /// Query atomic (sub-model) components with filtering.
    /// @param modelName Name filter (empty for all).
    /// @param modelType Type filter (empty for all).
    /// @param filePath  Model directory path.
    /// @return Vector of matching atomic model DTOs.
    virtual std::vector<cosmo::Model::MsgAtomicModel> QueryAtomicModels(const std::string& modelName,
                                                                        const std::string& modelType,
                                                                        const std::string& filePath) = 0;

    /// Get the list of model component definitions (detection, recognition, etc.).
    /// @return Vector of model component descriptors.
    virtual std::vector<cosmo::Model::MsgModelComponent> GetModelComponents() = 0;
};

// Narrow-interface accessor — delegates to ServiceRegistry internally.

}  // namespace cosmo::service
