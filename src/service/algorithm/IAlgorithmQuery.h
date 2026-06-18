/// @file IAlgorithmQuery.h
/// @brief Read-only algorithm query and runtime access interface.
///        ISP split from IAlgorithmService.
///        Consumed by flow/task/, service/event/, service/model/, api/ handlers.
#pragma once

#include <string>
#include <vector>

#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service {

/// Read-only query and runtime access for algorithm packages.
///
/// Provides paginated query, runtime object retrieval, name/metadata lookups,
/// reverse model-to-algorithm mapping, and path accessors.  Most consumers
/// outside the API layer only need this narrow interface.
class IAlgorithmQuery {
public:
    virtual ~IAlgorithmQuery() = default;

    // ── Paginated Query ──

    /// Query algorithms with pagination and filters.
    /// @param algorithmUsage    Usage filter (empty for all).
    /// @param algorithmName     Name filter (empty for all).
    /// @param supplier          Supplier filter (empty for all).
    /// @param algorithmId       Specific ID filter (empty for all).
    /// @param algorithmCategory Category filter (empty for all).
    /// @param pageNum           Page number (1-based).
    /// @param pageSize          Page size.
    /// @param total             [out] Total matching record count.
    /// @return Vector of matching algorithm package metadata.
    virtual std::vector<algorithm::AlgorithmPacketInfo> Query(const std::string& algorithmUsage,
                                                              const std::string& algorithmName,
                                                              const std::string& supplier,
                                                              const std::string& algorithmId,
                                                              const std::string& algorithmCategory,
                                                              int pageNum, int pageSize, size_t& total) = 0;

    // ── Runtime Access ──

    /// Get the runtime algorithm object by ID.
    /// @param algorithmId Algorithm identifier.
    /// @return Shared pointer to the loaded algorithm, or nullptr.
    virtual cosmo::ActionAlgPtr GetAlgorithm(const std::string& algorithmId) = 0;

    /// Get the human-readable name of an algorithm.
    /// @param algorithmId Algorithm identifier.
    /// @return Algorithm name string, or empty if not found.
    virtual std::string GetAlgorithmName(const std::string& algorithmId) = 0;

    /// Get algorithm metadata JSON string.
    /// @param algorithmId Algorithm identifier.
    /// @return Metadata JSON string, or empty if not found.
    virtual std::string GetMetaData(const std::string& algorithmId) = 0;

    /// Find all algorithms that use a given model.
    /// @param modelId Model identifier.
    /// @return List of algorithm IDs that reference this model.
    virtual std::vector<std::string> GetAlgorithmsByModelId(const std::string& modelId) = 0;

    // ── Specialized Queries ──

    /// Get algorithms relevant to passenger/vehicle flow counting.
    /// @return List of algorithm identity records matching flow categories.
    virtual std::vector<algorithm::AlgorithmLocalInfo> GetPassFlowAlgorithms() = 0;

    // ── Path Accessors ──

    /// Get the base directory for algorithm packages.
    virtual std::string GetAlgorithmPath() = 0;

    /// Get the path to the actions JSON definition file.
    virtual std::string GetActionsJsonPath() = 0;
};

}  // namespace cosmo::service
