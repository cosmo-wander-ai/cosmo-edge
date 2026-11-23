// ModelImporter.cc — Import-related operations for ModelImportExporter.
// Split from ModelImportExporter.cc to reduce file size (DEBT-007).

// clang-format off
#include "service/detail/ServiceRegistry.h"
#include "service/model/impl/ModelImportExporter.h"
// clang-format on

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <regex>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"
#include "service/model/impl/ModelConfigParser.h"
#include "util/ArchiveListingValidator.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/Exec.h"
#include "util/JsonFileUtil.h"
#include "util/NnBackendConstants.h"
#include "util/PathUtil.h"
#include "util/ResourceBudget.h"
#include "util/UuidUtil.h"

namespace cosmo::service {

namespace {

    constexpr size_t kMaxArchiveEntries = 10000;

    enum class ArchiveKind {
        kUnknown,
        kZip,
        kTarGzip,
    };

    ArchiveKind DetectArchiveKind(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        std::array<unsigned char, 4> header{};
        if (!stream.read(reinterpret_cast<char*>(header.data()), header.size())) {
            return ArchiveKind::kUnknown;
        }
        if (header[0] == 'P' && header[1] == 'K' &&
            ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
             (header[2] == 7 && header[3] == 8))) {
            return ArchiveKind::kZip;
        }
        if (header[0] == 0x1f && header[1] == 0x8b) {
            return ArchiveKind::kTarGzip;
        }
        return ArchiveKind::kUnknown;
    }

    bool InspectArchiveListing(const std::string& archive_path, bool is_zip,
                               const std::string& extraction_root,
                               util::ArchiveListingInspection& inspection) {
        const auto format =
            is_zip ? util::ArchiveListingFormat::kZipVerbose : util::ArchiveListingFormat::kTarVerbose;
        if (!util::InspectArchiveListingFile(archive_path, format, kMaxArchiveEntries, inspection) ||
            inspection.total_bytes == 0) {
            LOG_WARN("[ImportModel] Failed to inspect archive member list: {}", archive_path);
            return false;
        }
        const auto budget = util::InspectStorageResourceBudget(extraction_root);
        if (!budget.valid) {
            throw util::ErrorMessage(util::ErrorEnum::SysErr, "Cannot inspect model extraction storage");
        }
        if (inspection.total_bytes > budget.usable_bytes) {
            throw util::ResourceLimitError("Insufficient safe disk space to extract the model archive",
                                           "archive-extraction", "model-archive", inspection.total_bytes,
                                           budget.usable_bytes, budget.reserve_bytes);
        }
        return true;
    }

    bool ValidateExtractedTree(const std::string& root, const util::ArchiveListingInspection& inspection) {
        namespace fs              = std::filesystem;
        size_t entry_count        = 0;
        std::uintmax_t total_size = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (++entry_count > kMaxArchiveEntries) {
                return false;
            }

            const auto link_status = it->symlink_status(ec);
            if (ec || fs::is_symlink(link_status) ||
                (!fs::is_directory(link_status) && !fs::is_regular_file(link_status))) {
                return false;
            }

            std::string resolved;
            if (!cosmo::path::ResolveExistingPathWithinRoot(root, it->path().string(),
                                                            cosmo::path::PathEntryType::kAny, resolved)) {
                return false;
            }
            const auto relative = fs::relative(it->path(), root, ec);
            if (ec || relative.empty()) {
                return false;
            }
            for (const auto& component : relative) {
                if (!cosmo::path::IsSafePathComponent(component.string())) {
                    return false;
                }
            }
            if (fs::is_regular_file(link_status)) {
                const auto size = fs::file_size(it->path(), ec);
                if (ec || size > inspection.largest_file_bytes || total_size > inspection.total_bytes ||
                    size > inspection.total_bytes - total_size) {
                    return false;
                }
                total_size += size;
            }
        }
        return !ec;
    }

    bool ResolveModelDestination(const std::string& models_dir, const std::string& component,
                                 std::string& destination) {
        return cosmo::path::IsSafePathComponent(component, 200) &&
               cosmo::path::ResolvePathWithinRoot(
                   models_dir, (std::filesystem::path(models_dir) / component).string(), destination);
    }

    bool ResolveManagedModelUpload(const std::string& path, std::string& resolved) {
        return cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetModelUploadTmpDir(), path,
                                                          cosmo::path::PathEntryType::kRegularFile,
                                                          resolved) ||
               cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetUploadPath(), path,
                                                          cosmo::path::PathEntryType::kRegularFile, resolved);
    }

    bool RemoveExistingModelDirectory(const std::string& models_dir, const std::string& destination) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto link_status = fs::symlink_status(destination, ec);
        if (ec) {
            if (ec == std::errc::no_such_file_or_directory) {
                return true;
            }
            return false;
        }
        if (!fs::exists(link_status)) {
            return true;
        }
        if (fs::is_symlink(link_status)) {
            return false;
        }
        std::string resolved;
        if (!cosmo::path::ResolveExistingPathWithinRoot(models_dir, destination,
                                                        cosmo::path::PathEntryType::kDirectory, resolved)) {
            return false;
        }
        fs::remove_all(resolved, ec);
        return !ec;
    }

    bool HasInvalidPersonAttributeOutput(const nlohmann::json& config) {
        const auto type = config.find("model_type");
        return type != config.end() && *type == "yolo26_person_attributes" &&
               !detail::ModelConfigParser::ParsePersonAttributes(config);
    }

}  // namespace

util::ErrorEnum ModelImportExporter::ImportFlatArchive(const std::string& temp_dir,
                                                       const std::string& models_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;

    std::string config_path = temp_dir + "/config.json";
    std::ifstream cfgFile(config_path);
    if (!cfgFile.is_open()) {
        LOG_WARN("{}", "[ImportModel] Cannot open config.json in flat archive");
        fs::remove_all(temp_dir, ec);
        return util::ErrorEnum::InvalidParam;
    }
    std::stringstream cfgBuf;
    cfgBuf << cfgFile.rdbuf();
    cfgFile.close();

    std::string alg_code  = "0000000";
    std::string modelName = "imported";
    std::string version   = "V1.0.0";
    try {
        auto doc = nlohmann::json::parse(cfgBuf.str());
        if (HasInvalidPersonAttributeOutput(doc)) {
            LOG_WARN("{}", "[ImportModel] Invalid person attribute output declaration in config.json");
            fs::remove_all(temp_dir, ec);
            return util::ErrorEnum::InvalidParam;
        }
        if (doc.is_object()) {
            if (doc.contains("algorithm_code") && doc["algorithm_code"].is_string())
                alg_code = doc["algorithm_code"].get<std::string>();
            if (doc.contains("version") && doc["version"].is_string())
                version = doc["version"].get<std::string>();
            if (doc.contains("models") && doc["models"].is_array() && !doc["models"].empty()) {
                const auto& m = doc["models"][0];
                if (m.contains("name") && m["name"].is_string())
                    modelName = m["name"].get<std::string>();
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("[ImportModel] Failed to parse config.json: {}", e.what());
    }

    static const std::regex kAlgorithmCodePattern("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$");
    static const std::regex kVersionPattern("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$");
    if (!std::regex_match(alg_code, kAlgorithmCodePattern) || !std::regex_match(version, kVersionPattern) ||
        !cosmo::path::IsSafePathComponent(modelName, 64)) {
        LOG_WARN("{}", "[ImportModel] Reject unsafe model metadata components");
        fs::remove_all(temp_dir, ec);
        return util::ErrorEnum::InvalidParam;
    }

    std::replace_if(
        modelName.begin(), modelName.end(),
        [](char c) {
            return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                   c == '>' || c == '|' || c == ' ';
        },
        '_');

    std::string dir_name =
        std::string(cosmo::util::kNewDirPrefix) + alg_code + "_" + modelName + "_" + version;
    std::string dest_dir;
    if (!ResolveModelDestination(models_dir, dir_name, dest_dir)) {
        fs::remove_all(temp_dir, ec);
        return util::ErrorEnum::InvalidParam;
    }

    // Remove existing if present
    if (!RemoveExistingModelDirectory(models_dir, dest_dir)) {
        fs::remove_all(temp_dir, ec);
        return util::ErrorEnum::InvalidParam;
    }

    fs::rename(temp_dir, dest_dir, ec);
    if (ec) {
        // rename may fail across filesystems, fall back to copy
        ec.clear();
        fs::create_directories(dest_dir, ec);
        if (ec) {
            fs::remove_all(temp_dir, ec);
            return util::ErrorEnum::SysErr;
        }
        for (const auto& entry : fs::directory_iterator(temp_dir)) {
            ec.clear();
            fs::copy(entry.path(), fs::path(dest_dir) / entry.path().filename(),
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::error_code cleanup_ec;
                fs::remove_all(dest_dir, cleanup_ec);
                fs::remove_all(temp_dir, cleanup_ec);
                return util::ErrorEnum::SysErr;
            }
        }
        fs::remove_all(temp_dir, ec);
    }

    if (set_model_path_mapping_) {
        set_model_path_mapping_(alg_code, dest_dir);
    }

    LOG_INFO("[ImportModel] Imported flat archive as: {}", dir_name);
    return util::ErrorEnum::Success;
}

util::ErrorEnum ModelImportExporter::ImportDirectoryArchive(const std::string& temp_dir,
                                                            const std::string& models_dir,
                                                            int& imported_count) {
    namespace fs = std::filesystem;
    std::error_code ec;

    for (const auto& dirEntry : fs::directory_iterator(temp_dir)) {
        std::error_code status_ec;
        if (dirEntry.is_symlink(status_ec) || !dirEntry.is_directory(status_ec))
            continue;
        std::string sub_dir      = dirEntry.path().string();
        std::string sub_dir_name = dirEntry.path().filename().string();
        std::string resolved_sub_dir;
        if (!cosmo::path::IsSafePathComponent(sub_dir_name, 200) ||
            !cosmo::path::ResolveExistingPathWithinRoot(
                temp_dir, sub_dir, cosmo::path::PathEntryType::kDirectory, resolved_sub_dir)) {
            LOG_WARN("[ImportModel] Skipping unsafe model directory: {}", sub_dir_name);
            continue;
        }
        sub_dir = std::move(resolved_sub_dir);

        // Validate: must have config.json
        if (!fs::exists(sub_dir + "/config.json")) {
            LOG_WARN("[ImportModel] Skipping directory without config.json: {}", sub_dir_name);
            continue;
        }

        nlohmann::json config;
        if (util::JsonFileUtil::ReadJsonFile(sub_dir + "/config.json", config) == util::ErrorEnum::Success &&
            HasInvalidPersonAttributeOutput(config)) {
            LOG_WARN("[ImportModel] Invalid person attribute output declaration in {}", sub_dir_name);
            continue;
        }

        // Must have a model file (extension depends on platform)
        bool has_model = fs::exists(sub_dir + "/model" + std::string(cosmo::util::kModelFileExt));
        if (!has_model) {
            has_model =
                std::any_of(fs::directory_iterator(sub_dir), fs::directory_iterator(), [](const auto& f) {
                    return f.path().extension() == cosmo::util::kModelFileExt ||
                           f.path().extension() == ".bmodel" || f.path().extension() == ".onnx" ||
                           f.path().extension() == ".rknn";
                });
        }
        if (!has_model) {
            LOG_WARN("[ImportModel] Skipping directory without model file: {}", sub_dir_name);
            continue;
        }

        std::string dest_dir;
        if (!ResolveModelDestination(models_dir, sub_dir_name, dest_dir)) {
            LOG_WARN("[ImportModel] Skipping unsafe destination directory: {}", sub_dir_name);
            continue;
        }

        // Remove existing if present
        if (!RemoveExistingModelDirectory(models_dir, dest_dir)) {
            LOG_WARN("[ImportModel] Refusing to replace unsafe model destination: {}", dest_dir);
            continue;
        }

        fs::rename(sub_dir, dest_dir, ec);
        if (ec) {
            // Fall back to recursive copy
            fs::create_directories(dest_dir, ec);
            fs::copy(sub_dir, dest_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                     ec);
            if (ec) {
                LOG_WARN("[ImportModel] Failed to copy model dir {}: {}", sub_dir_name, ec.message());
                continue;
            }
        }

        // Extract alg_code from sub_dir_name (format: prod_BM1688_{alg_code}_...)
        std::string sub_alg_code;
        std::regex algPattern(cosmo::util::kPlatformDirRegex);
        std::smatch matches;
        if (std::regex_match(sub_dir_name, matches, algPattern) && matches.size() > 1) {
            sub_alg_code = matches[1].str();
        }
        if (!sub_alg_code.empty() && set_model_path_mapping_) {
            set_model_path_mapping_(sub_alg_code, dest_dir);
        }

        imported_count++;
        LOG_INFO("[ImportModel] Imported model directory: {}", sub_dir_name);
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum ModelImportExporter::ImportModel(const std::string& archivePath) {
    namespace fs = std::filesystem;

    std::string managed_archive_path;
    if (!ResolveManagedModelUpload(archivePath, managed_archive_path)) {
        LOG_WARN("[ImportModel] Archive is not a managed upload: {}", archivePath);
        return util::ErrorEnum::FileNotExist;
    }

    size_t archive_size = 0;
    try {
        archive_size = fs::file_size(managed_archive_path);
    } catch (const std::exception& e) {
        LOG_WARN("[ImportModel] Failed to get archive size: {}", e.what());
    }
    if (archive_size == 0) {
        LOG_WARN("[ImportModel] Archive size is invalid: {}", archive_size);
        return util::ErrorEnum::InvalidParam;
    }
    LOG_INFO("[ImportModel] Starting managed import, size: {} bytes", archive_size);

    const auto temporary_root = cosmo::path::GetTemporaryDirPath();
    const auto archive_kind   = DetectArchiveKind(managed_archive_path);
    const bool is_zip         = archive_kind == ArchiveKind::kZip;
    util::ArchiveListingInspection inspection;
    if (archive_kind == ArchiveKind::kUnknown ||
        !InspectArchiveListing(managed_archive_path, is_zip, temporary_root, inspection)) {
        return util::ErrorEnum::InvalidParam;
    }

    // 1. Create temp extraction directory
    std::string temp_dir = (fs::path(temporary_root) / ("model_import_" + util::GenerateUUID())).string();
    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        LOG_WARN("[ImportModel] Failed to create temp directory: {}", temp_dir);
        return util::ErrorEnum::SysErr;
    }

    // 2. Extract archive (detect format: tar.gz or zip)
    std::string out_str;
    std::vector<std::string> extract_argv;
    if (is_zip) {
        LOG_INFO("{}", "[ImportModel] Extracting managed ZIP archive");
        extract_argv = {"unzip", "-o", managed_archive_path, "-d", temp_dir};
    } else {
        LOG_INFO("{}", "[ImportModel] Extracting managed tar.gz archive");
        extract_argv = {"tar", "-xzf", managed_archive_path, "-C", temp_dir};
    }

    int extract_ret = util::Exec(extract_argv, out_str);
    if (extract_ret != 0) {
        LOG_WARN("[ImportModel] Extract failed (ret={}): {}", extract_ret, out_str);
        fs::remove_all(temp_dir, ec);
        return util::ErrorEnum::SysErr;
    }
    if (!ValidateExtractedTree(temp_dir, inspection)) {
        LOG_WARN("{}", "[ImportModel] Extracted archive violates path, type, count, or size limits");
        fs::remove_all(temp_dir, ec);
        return util::ErrorEnum::InvalidParam;
    }

    // Log extracted contents for debugging
    {
        std::string listing = std::accumulate(
            fs::recursive_directory_iterator(temp_dir), fs::recursive_directory_iterator(), std::string{},
            [](std::string s, const auto& entry) { return std::move(s) + "\n  " + entry.path().string(); });
        LOG_INFO("[ImportModel] Extracted contents:{}", listing);
    }

    // 3. Find model directory(ies)
    const std::string models_dir = get_model_path_();
    int imported_count           = 0;

    // Check if the extracted content is a flat structure
    bool flat_structure = false;
    if (fs::exists(temp_dir + "/config.json")) {
        for (const auto& f : fs::directory_iterator(temp_dir)) {
            if (!f.is_regular_file())
                continue;
            auto ext = f.path().extension().string();
            if (ext == cosmo::util::kModelFileExt || ext == ".bmodel" || ext == ".onnx" || ext == ".rknn") {
                flat_structure = true;
                break;
            }
        }
    }

    if (flat_structure) {
        auto err = ImportFlatArchive(temp_dir, models_dir);
        if (err != util::ErrorEnum::Success)
            return err;
        imported_count = 1;
    } else {
        ImportDirectoryArchive(temp_dir, models_dir, imported_count);
    }

    // Cleanup
    fs::remove_all(temp_dir, ec);

    // Cleanup uploaded archive
    try {
        fs::remove(managed_archive_path);
    } catch (const std::exception& e) {
        LOG_WARN("[ImportModel] Failed to remove managed temp archive: {}", e.what());
    }

    if (imported_count == 0) {
        LOG_WARN("{}", "[ImportModel] No valid model directories found in archive");
        return util::ErrorEnum::InvalidParam;
    }

    LOG_INFO("[ImportModel] Successfully imported {} model(s)", imported_count);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo::service
