/// @file IAlgorithmLayout.h
/// @brief Algorithm layout configuration interface (AIBox platform).
///        ISP split from IAlgorithmService.
///        Consumed by MessageAlgorithmHandler.
#pragma once

#include <string>
#include <vector>

#include "util/ErrorCode.h"
#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service {

/// Layout configuration operations for algorithm orchestration (AIBox platform).
///
/// Provides save, query, and export operations for algorithm layout
/// configurations — the visual action-graph editor used by the AIBox platform.
class IAlgorithmLayout {
public:
    virtual ~IAlgorithmLayout() = default;

    /// Save or update an algorithm layout configuration.
    /// @param req Layout save request containing the layout definition.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum LayoutSave(const algorithm::LayoutSaveReq& req) = 0;

    /// Retrieve detailed layout configuration by ID.
    /// @param id        Layout ID.
    /// @param filePath  Path to the algorithm directory for resolution.
    /// @param outResult [out] Populated layout detail.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum GetLayoutDetail(const std::string& id, const std::string& filePath,
                                                   algorithm::LayoutDetailResult& outResult) = 0;

    /// List all layouts matching the given filters.
    /// @param supplier  Supplier name filter (empty for all).
    /// @param usage     Usage type filter.
    /// @param filePath  Path to the algorithm directory.
    /// @param outResult [out] Populated layout list.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum GetLayoutList(const std::string& supplier, int usage,
                                                 const std::string& filePath,
                                                 algorithm::LayoutListResult& outResult) = 0;

    /// Export a single layout as a portable package.
    /// @param code      Algorithm code.
    /// @param name      Algorithm name.
    /// @param category  Category string.
    /// @param supplier  Supplier string.
    /// @param versionId Version identifier.
    /// @param outResult [out] Export result with file path.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum LayoutExportSingle(const std::string& code, const std::string& name,
                                                      const std::string& category,
                                                      const std::string& supplier,
                                                      const std::string& versionId,
                                                      algorithm::LayoutExportResult& outResult) = 0;

    /// Export multiple layouts as a single package.
    /// @param algorithmName     Name filter.
    /// @param supplier          Supplier filter.
    /// @param algorithmUsage    Usage filter.
    /// @param algorithmCategory Category filter.
    /// @param algorithmIds      List of algorithm IDs to export.
    /// @param outResult         [out] Export result with file path.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum LayoutExportAll(const std::string& algorithmName,
                                                   const std::string& supplier,
                                                   const std::string& algorithmUsage,
                                                   const std::string& algorithmCategory,
                                                   const std::vector<std::string>& algorithmIds,
                                                   algorithm::LayoutExportResult& outResult) = 0;

    /// List available atomic actions for a given usage type.
    /// @param actionUsage Usage type code.
    /// @param filePath    Path to the actions directory.
    /// @param outResult   [out] Populated action list.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum GetAtomicActionList(int actionUsage, const std::string& filePath,
                                                       algorithm::AtomicActionListResult& outResult) = 0;
};

}  // namespace cosmo::service
