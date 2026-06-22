/// @file IAlgorithmCrud.h
/// @brief Algorithm installation, deletion, update, and hot-reload interface.
///        ISP split from IAlgorithmService.
///        Consumed by MessageAlgorithmHandler, AlgorithmLayoutMng.
#pragma once

#include <string>

#include "util/ErrorCode.h"
#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service {

/// Write operations for algorithm packages: install, delete, update, reload.
class IAlgorithmCrud {
public:
    virtual ~IAlgorithmCrud() = default;

    // ── Algorithm Installation ──

    /// Install an algorithm package from a local archive file.
    /// @param filePath Path to the algorithm package archive.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Add(const std::string& filePath) = 0;

    /// Install an algorithm package from a pre-parsed configuration.
    /// @param modelCfg Parsed algorithm package metadata.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Add(const algorithm::AlgorithmPacketInfo& modelCfg) = 0;

    /// Install an algorithm from JSON-based parameters.
    /// @param algorithmName     Human-readable algorithm name.
    /// @param algorithmCategory Category code (e.g. video / image).
    /// @param algorithmUsage    Usage code (e.g. detection / recognition).
    /// @param remark            Optional description text.
    /// @param eventType         Event type identifier for alarm binding.
    /// @param filePath          Path to the algorithm archive.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum AddFromJson(const std::string& algorithmName, int algorithmCategory,
                                               int algorithmUsage, const std::string& remark,
                                               const std::string& eventType, const std::string& filePath) = 0;

    // ── Algorithm Modification ──

    /// Delete an algorithm by ID.
    /// @param algorithmId Algorithm identifier to delete.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Delete(const std::string& algorithmId) = 0;

    /// Update algorithm metadata.
    /// @param algorithmId       Algorithm identifier.
    /// @param algorithmName     New name.
    /// @param algorithmCategory New category code.
    /// @param remark            New description text.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum Update(const std::string& algorithmId, const std::string& algorithmName,
                                          int algorithmCategory, const std::string& remark) = 0;

    /// Reload an algorithm from its on-disk file (hot-reload).
    /// @param filePath Path to the algorithm definition file.
    /// @return ErrorEnum::kSuccess on success.
    virtual cosmo::util::ErrorEnum ReloadAlgorithmFromFile(const std::string& filePath) = 0;
};

}  // namespace cosmo::service
