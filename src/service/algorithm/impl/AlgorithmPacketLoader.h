// Algorithm packet loading and parsing — extracted from AlgorithmServiceImpl.
// Responsible for: reading algorithm ZIP/JSON files from disk, parsing
// AlgorithmPacketInfo structs, and activating processdata via ActionService.

#pragma once

#include <string>
#include <vector>

#include "nlohmann/json_fwd.hpp"
#include "util/ErrorCode.h"
#include "util/dto/AlgorithmPacketDto.h"

namespace cosmo::service::detail {

/// Stateless helper for loading algorithm packages from the filesystem.
/// All methods are static — no mutable state.
class AlgorithmPacketLoader {
public:
    /// Unzip a .zip algorithm package file and return the extracted directory path.
    /// Returns empty string on failure.
    static std::string UnzipPackageFile(const std::string& filePath);

    /// Load algorithm packages from a directory of .zip files.
    /// Deduplicates by id, keeping the newer version.
    static std::vector<algorithm::AlgorithmPacketInfo> LoadFromZipDirectory(const std::string& path);

    /// Scan a directory for .json algorithm layout files and parse each one.
    /// Skips reserved filenames (actions.json, atomicModels.json, etc.).
    /// Returns loaded algorithm packets.
    static std::vector<algorithm::AlgorithmPacketInfo> LoadFromJsonDirectory(
        const std::string& directoryPath);

    /// Parse a single algorithm JSON file into an AlgorithmPacketInfo.
    /// Returns false if the file lacks algorithmId/algorithmCode.
    static bool ParsePacketFromJson(const nlohmann::json& doc, const std::string& filePath,
                                    std::string& algorithmCode, algorithm::AlgorithmPacketInfo& outPacket);

    /// Parse and reload a single algorithm JSON file.
    /// Activates processdata via ActionService.
    /// Returns the parsed packet and its code, or error.
    static cosmo::util::ErrorEnum ReloadFromFile(const std::string& filePath, std::string& outAlgorithmCode,
                                                 algorithm::AlgorithmPacketInfo& outPacket);

    /// Activate processdata for a packet — dispatches to ActionService.
    static void ActivateProcessdata(algorithm::AlgorithmPacketInfo& packet);
};

}  // namespace cosmo::service::detail
