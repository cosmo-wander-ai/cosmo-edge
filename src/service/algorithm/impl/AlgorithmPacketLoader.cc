// AlgorithmPacketLoader — Algorithm packet loading and parsing — extracted from AlgorithmServiceImpl.

#include "service/algorithm/impl/AlgorithmPacketLoader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "nlohmann/json.hpp"
#include "service/algorithm/IActionService.h"
#include "service/algorithm/impl/AlgorithmJsonCodec.h"
#include "service/algorithm/impl/AlgorithmLayoutMng.h"
#include "service/algorithm/impl/AlgorithmValidator.h"
#include "service/detail/ServiceRegistry.h"
#include "util/ArchiveListingValidator.h"
#include "util/DateTimeFormat.h"
#include "util/Exception.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/ResourceBudget.h"

namespace cosmo::service::detail {

namespace {

    constexpr size_t kMaxAlgorithmArchiveEntries = 10000;

    enum class AlgorithmArchiveKind {
        kUnknown,
        kZip,
        kTarGzip,
    };

    AlgorithmArchiveKind DetectAlgorithmArchiveKind(const std::string& path, const std::string& filename) {
        std::ifstream stream(path, std::ios::binary);
        std::array<unsigned char, 4> header{};
        if (!stream.read(reinterpret_cast<char*>(header.data()), header.size())) {
            return AlgorithmArchiveKind::kUnknown;
        }

        const bool is_zip = header[0] == 'P' && header[1] == 'K' &&
                            ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
                             (header[2] == 7 && header[3] == 8));
        if (filename.size() > 4 && filename.compare(filename.size() - 4, 4, ".zip") == 0) {
            return is_zip ? AlgorithmArchiveKind::kZip : AlgorithmArchiveKind::kUnknown;
        }

        const bool has_tar_gzip_extension =
            (filename.size() > 7 && filename.compare(filename.size() - 7, 7, ".tar.gz") == 0) ||
            (filename.size() > 4 && filename.compare(filename.size() - 4, 4, ".tgz") == 0);
        if (has_tar_gzip_extension && header[0] == 0x1f && header[1] == 0x8b) {
            return AlgorithmArchiveKind::kTarGzip;
        }
        return AlgorithmArchiveKind::kUnknown;
    }

    bool InspectAlgorithmArchiveListing(const std::string& archive_path, AlgorithmArchiveKind kind,
                                        const std::string& extraction_root,
                                        cosmo::util::ArchiveListingInspection& inspection) {
        const auto format = kind == AlgorithmArchiveKind::kZip
                                ? cosmo::util::ArchiveListingFormat::kZipVerbose
                                : cosmo::util::ArchiveListingFormat::kTarVerbose;
        if (!cosmo::util::InspectArchiveListingFile(archive_path, format, kMaxAlgorithmArchiveEntries,
                                                    inspection) ||
            inspection.total_bytes == 0) {
            LOG_WARN("Failed to inspect algorithm archive: {}", archive_path);
            return false;
        }
        const auto budget = cosmo::util::InspectStorageResourceBudget(extraction_root);
        if (!budget.valid) {
            throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::SysErr,
                                            "Cannot inspect algorithm extraction storage");
        }
        if (inspection.total_bytes > budget.usable_bytes) {
            throw cosmo::util::ResourceLimitError(
                "Insufficient safe disk space to extract the algorithm archive", "archive-extraction",
                "algorithm", inspection.total_bytes, budget.usable_bytes, budget.reserve_bytes);
        }
        return true;
    }

    std::string AlgorithmExtractDirectoryName(const std::string& filename, AlgorithmArchiveKind kind) {
        if (kind == AlgorithmArchiveKind::kTarGzip) {
            if (filename.size() > 7 && filename.compare(filename.size() - 7, 7, ".tar.gz") == 0) {
                return filename.substr(0, filename.size() - 7);
            }
            return filename.substr(0, filename.size() - 4);
        }
        return filename.substr(0, filename.size() - 4);
    }

}  // namespace

std::string AlgorithmPacketLoader::UnzipPackageFile(const std::string& filePath) {
    std::error_code ec;
    const auto absolute_archive = std::filesystem::absolute(filePath, ec);
    if (ec || absolute_archive.filename().empty() ||
        !cosmo::path::IsSafePathComponent(absolute_archive.filename().string(), 255)) {
        return {};
    }

    std::string archive_path;
    if (!cosmo::path::ResolveExistingPathWithinRoot(absolute_archive.parent_path().string(),
                                                    absolute_archive.string(),
                                                    cosmo::path::PathEntryType::kRegularFile, archive_path)) {
        LOG_WARN("Reject unsafe algorithm archive path: {}", filePath);
        return {};
    }

    const auto archive_size = std::filesystem::file_size(archive_path, ec);
    if (ec || archive_size == 0) {
        LOG_WARN("Reject invalid algorithm archive size: {}", filePath);
        return {};
    }

    const std::string filename = absolute_archive.filename().string();
    const auto archive_kind    = DetectAlgorithmArchiveKind(archive_path, filename);
    cosmo::util::ArchiveListingInspection inspection;
    if (archive_kind == AlgorithmArchiveKind::kUnknown ||
        !InspectAlgorithmArchiveListing(archive_path, archive_kind, absolute_archive.parent_path().string(),
                                        inspection)) {
        LOG_WARN("Reject invalid algorithm archive: {}", filePath);
        return {};
    }

    const std::string directory_name = AlgorithmExtractDirectoryName(filename, archive_kind);
    if (!cosmo::path::IsSafePathComponent(directory_name, 255)) {
        return {};
    }

    std::string upload_path;
    if (!cosmo::path::ResolvePathWithinRoot(absolute_archive.parent_path().string(),
                                            (absolute_archive.parent_path() / directory_name).string(),
                                            upload_path)) {
        return {};
    }

    const auto output_status = std::filesystem::symlink_status(upload_path, ec);
    if ((!ec && std::filesystem::exists(output_status)) ||
        (ec && ec != std::errc::no_such_file_or_directory)) {
        LOG_WARN("Reject existing algorithm extraction destination: {}", upload_path);
        return {};
    }
    ec.clear();
    if (!std::filesystem::create_directory(upload_path, ec) || ec) {
        LOG_WARN("Cannot create algorithm extraction destination: {}", upload_path);
        return {};
    }
    std::filesystem::permissions(upload_path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    if (ec) {
        std::error_code cleanup_ec;
        std::filesystem::remove_all(upload_path, cleanup_ec);
        return {};
    }

    const std::vector<std::string> extract_argv =
        archive_kind == AlgorithmArchiveKind::kZip
            ? std::vector<std::string>{"unzip", "-q", archive_path, "-d", upload_path}
            : std::vector<std::string>{"tar",       "--extract",       "--gzip",
                                       "--file",    archive_path,      "--directory",
                                       upload_path, "--no-same-owner", "--no-same-permissions"};
    std::string output;
    const int exit_code = cosmo::util::Exec(extract_argv, output);
    if (exit_code != 0 || !cosmo::path::ValidateDirectoryTreeWithinRoot(
                              upload_path, kMaxAlgorithmArchiveEntries, inspection.largest_file_bytes,
                              inspection.total_bytes)) {
        LOG_WARN("Extract algorithm archive {} failed or produced an unsafe tree: {}", filePath, output);
        std::error_code cleanup_ec;
        std::filesystem::remove_all(upload_path, cleanup_ec);
        return {};
    }

    LOG_INFO("extract file OK {} -> {}", filePath, upload_path);
    return upload_path;
}

std::vector<algorithm::AlgorithmPacketInfo> AlgorithmPacketLoader::LoadFromZipDirectory(
    const std::string& path) {
    std::vector<algorithm::AlgorithmPacketInfo> algorithms;
    LOG_INFO("path:{}", path);
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
            if (!entry.is_regular_file(ec))
                continue;
            files.push_back(entry.path());
        }
    }
    LOG_INFO("path:{} total {} algorithm package files", path, files.size());
    for (auto& file : files) {
        auto model_path_name = file.string();
        algorithm::AlgorithmPacketInfo algorithmInfo;
        std::error_condition ret =
            detail::AlgorithmValidator::ParseAndValidatePacket(model_path_name, algorithmInfo);
        if (cosmo::util::ErrorEnum::Success != ret) {
            LOG_WARN("Load {} Get {}", model_path_name, ret.message());
            continue;
        }
        ActivateProcessdata(algorithmInfo);
        // Backfill empty id from algorithmCode to prevent dedup false-positives.
        if (algorithmInfo.id.empty() && algorithmInfo.algorithmCode > 0) {
            algorithmInfo.id = std::to_string(algorithmInfo.algorithmCode);
        }
        algorithmInfo.filePath = model_path_name;
        // Deduplicate: keep the newer version
        auto it = std::find_if(
            algorithms.begin(), algorithms.end(),
            [&](const algorithm::AlgorithmPacketInfo& info) { return info.id == algorithmInfo.id; });
        if (it != algorithms.end()) {
            if ((*it).createTime > algorithmInfo.createTime) {
                cosmo::util::RemoveFile(algorithmInfo.filePath);
            } else {
                cosmo::util::RemoveFile((*it).filePath);
                *it = algorithmInfo;
            }
            continue;
        }
        algorithms.push_back(algorithmInfo);
    }
    LOG_INFO("path:{} successfully loaded {} algorithm packages", path, algorithms.size());
    return algorithms;
}

namespace {

    // Helper: get string from json with type coercion (int->string)
    std::string GetStringOrInt(const nlohmann::json& doc, const std::string& key) {
        if (!doc.contains(key))
            return "";
        const auto& v = doc[key];
        if (v.is_string())
            return v.get<std::string>();
        if (v.is_number_integer())
            return std::to_string(v.get<int64_t>());
        return "";
    }

    int GetIntOrString(const nlohmann::json& doc, const std::string& key, int default_val) {
        if (!doc.contains(key))
            return default_val;
        const auto& v = doc[key];
        if (v.is_number_integer())
            return v.get<int>();
        if (v.is_string()) {
            try {
                return std::stoi(v.get<std::string>());
            } catch (...) {
                return default_val;
            }
        }
        return default_val;
    }

}  // namespace

bool AlgorithmPacketLoader::ParsePacketFromJson(const nlohmann::json& doc, const std::string& filePath,
                                                std::string& algorithmCode,
                                                algorithm::AlgorithmPacketInfo& newPacket) {
    algorithmCode = GetStringOrInt(doc, "algorithmId");
    if (algorithmCode.empty())
        algorithmCode = GetStringOrInt(doc, "algorithmCode");
    if (algorithmCode.empty())
        return false;

    newPacket.id = algorithmCode;
    try {
        newPacket.algorithmCode = std::stoi(algorithmCode);
    } catch (const std::exception&) {
        // ignore
    }
    if (doc.contains("algorithmName") && doc["algorithmName"].is_string())
        newPacket.algorithmName = doc["algorithmName"].get<std::string>();
    // Fallback: parse algorithmName from filename if JSON field is missing
    // Filename format: {code}_{name}_{timestamp}.json or {code}_{name}.json
    if (newPacket.algorithmName.empty() && !filePath.empty()) {
        std::string fname = std::filesystem::path(filePath).stem().string();  // remove .json
        // Skip the "{code}_" prefix
        size_t first_underscore = fname.find('_');
        if (first_underscore != std::string::npos && first_underscore + 1 < fname.size()) {
            std::string rest = fname.substr(first_underscore + 1);
            // Remove trailing "_{timestamp}" if present (timestamp is all digits)
            size_t last_underscore = rest.rfind('_');
            if (last_underscore != std::string::npos && last_underscore + 1 < rest.size()) {
                std::string tail = rest.substr(last_underscore + 1);
                bool all_digits  = !tail.empty() && std::all_of(tail.begin(), tail.end(), ::isdigit);
                if (all_digits)
                    rest = rest.substr(0, last_underscore);
            }
            newPacket.algorithmName = rest;
        }
    }
    newPacket.algorithmCategory = GetIntOrString(doc, "algorithmCategory", 0);
    newPacket.algorithmUsage    = GetIntOrString(doc, "algorithmUsage", 0);
    if (doc.contains("remark") && doc["remark"].is_string())
        newPacket.remark = doc["remark"].get<std::string>();
    if (doc.contains("eventType") && doc["eventType"].is_string())
        newPacket.eventType = doc["eventType"].get<std::string>();
    if (doc.contains("supplier") && doc["supplier"].is_string())
        newPacket.supplier = doc["supplier"].get<std::string>();
    else
        newPacket.supplier = "CWAI";
    if (doc.contains("algorithmMetadata") && doc["algorithmMetadata"].is_string())
        newPacket.algorithmMetadata = doc["algorithmMetadata"].get<std::string>();
    if (doc.contains("algorithmProcessdata") && doc["algorithmProcessdata"].is_string())
        newPacket.algorithmProcessdata = doc["algorithmProcessdata"].get<std::string>();
    if (doc.contains("atomicList") && doc["atomicList"].is_string())
        newPacket.atomicList = doc["atomicList"].get<std::string>();
    if (doc.contains("confVersionId") && doc["confVersionId"].is_string())
        newPacket.confVersionId = doc["confVersionId"].get<std::string>();
    if (doc.contains("confVersionName") && doc["confVersionName"].is_string())
        newPacket.confVersionName = doc["confVersionName"].get<std::string>();
    if (doc.contains("configVersionList") && doc["configVersionList"].is_array() &&
        !doc["configVersionList"].empty()) {
        std::string default_version_id = newPacket.confVersionId;
        if (default_version_id.empty())
            default_version_id = GetStringOrInt(doc, "confVersionId");
        for (const auto& version : doc["configVersionList"]) {
            if (version.contains("id") && version["id"].is_string() &&
                version["id"].get<std::string>() == default_version_id) {
                if (version.contains("algorithmUpdateTime") && version["algorithmUpdateTime"].is_number()) {
                    newPacket.algorithmUpdateTime =
                        std::to_string(version["algorithmUpdateTime"].get<uint64_t>());
                }
                break;
            }
        }
        if (newPacket.algorithmUpdateTime.empty() && !doc["configVersionList"].empty()) {
            const auto& first = doc["configVersionList"][0];
            if (first.contains("algorithmUpdateTime") && first["algorithmUpdateTime"].is_number())
                newPacket.algorithmUpdateTime = std::to_string(first["algorithmUpdateTime"].get<uint64_t>());
        }
    }
    if (newPacket.algorithmUpdateTime.empty())
        newPacket.algorithmUpdateTime =
            std::to_string(static_cast<int64_t>(cosmo::util::GetCurrentDateTime().ToTimeStamp()) * 1000);
    if (doc.contains("createTime") && doc["createTime"].is_number_integer())
        newPacket.createTime = doc["createTime"].get<int64_t>();
    if (doc.contains("updateTime") && doc["updateTime"].is_number_integer())
        newPacket.updateTime = doc["updateTime"].get<int64_t>();

    newPacket.filePath = filePath;
    return true;
}

void AlgorithmPacketLoader::ActivateProcessdata(algorithm::AlgorithmPacketInfo& packet) {
    if (packet.algorithmProcessdata.empty() || packet.algorithmProcessdata == "[]")
        return;
    std::string process_data_json =
        detail::NormalizeAlgorithmProcessdataParamValues(packet.algorithmProcessdata);
    packet.processdata = std::make_shared<cosmo::ActionAlg>();
    if (cosmo::util::DecodeJson(process_data_json, packet.processdata->workFlow)) {
        packet.processdata->algorithmCode       = packet.id;
        packet.processdata->algorithmName       = packet.algorithmName;
        packet.processdata->algorithmUpdateTime = packet.algorithmUpdateTime;
        packet.processdata->category            = std::to_string(packet.algorithmCategory);
        // Dispatch to the corresponding action manager based on algorithmUsage
        if (packet.algorithmUsage == 2) {
            ServiceRegistry::Instance().Get<IActionService>().UpdatePicActionAlg(*packet.processdata);
        } else {
            ServiceRegistry::Instance().Get<IActionService>().UpdateActionAlg(*packet.processdata);
        }
    }
}

std::vector<algorithm::AlgorithmPacketInfo> AlgorithmPacketLoader::LoadFromJsonDirectory(
    const std::string& directoryPath) {
    std::vector<algorithm::AlgorithmPacketInfo> results;
    if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath))
        return results;

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath, ec)) {
        if (!entry.is_regular_file())
            continue;
        std::string filename = entry.path().filename().string();
        if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".json")
            continue;
        if (filename == "actions.json" || filename == "atomicModels.json" || filename == "list.json" ||
            filename == "algorithms.json")
            continue;

        std::string file_path = entry.path().string();
        nlohmann::json doc;
        if (cosmo::util::JsonFileUtil::ReadJsonFile(file_path, doc) != cosmo::util::ErrorEnum::Success ||
            !doc.is_object())
            continue;

        std::string algorithm_code;
        algorithm::AlgorithmPacketInfo newPacket;
        if (!ParsePacketFromJson(doc, file_path, algorithm_code, newPacket))
            continue;
        ActivateProcessdata(newPacket);
        results.push_back(std::move(newPacket));
    }
    LOG_INFO("LoadFromJsonDirectory {} found {} new algorithms", directoryPath, results.size());
    return results;
}

cosmo::util::ErrorEnum AlgorithmPacketLoader::ReloadFromFile(const std::string& filePath,
                                                             std::string& outAlgorithmCode,
                                                             algorithm::AlgorithmPacketInfo& outPacket) {
    nlohmann::json doc;
    if (cosmo::util::JsonFileUtil::ReadJsonFile(filePath, doc) != cosmo::util::ErrorEnum::Success ||
        !doc.is_object()) {
        LOG_WARN("Failed to read or parse JSON file: {}", filePath);
        return cosmo::util::ErrorEnum::Failed;
    }

    if (!ParsePacketFromJson(doc, filePath, outAlgorithmCode, outPacket)) {
        LOG_WARN("Algorithm file {} missing algorithmId/algorithmCode", filePath);
        return cosmo::util::ErrorEnum::ParameterException;
    }
    ActivateProcessdata(outPacket);
    return cosmo::util::ErrorEnum::Success;
}

}  // namespace cosmo::service::detail
