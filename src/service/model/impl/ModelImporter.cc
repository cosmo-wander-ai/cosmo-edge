// ModelImporter.cc — Import-related operations for ModelImportExporter.
// Split from ModelImportExporter.cc to reduce file size (DEBT-007).

// clang-format off
#include "service/detail/ServiceRegistry.h"
#include "service/model/impl/ModelImportExporter.h"
// clang-format on

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <regex>
#include <sstream>

#include "nlohmann/json.hpp"
#include "util/ErrorCode.h"
#include "util/Exec.h"
#include "util/PathUtil.h"
#include "util/PlatformConstants.h"

namespace cosmo::service {

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

    std::replace_if(
        modelName.begin(), modelName.end(),
        [](char c) {
            return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                   c == '>' || c == '|' || c == ' ';
        },
        '_');

    std::string dir_name =
        std::string(cosmo::util::kPlatformDirPrefix) + alg_code + "_" + modelName + "_" + version;
    std::string dest_dir = (fs::path(models_dir) / dir_name).string();

    // Remove existing if present
    if (fs::exists(dest_dir))
        fs::remove_all(dest_dir, ec);

    fs::rename(temp_dir, dest_dir, ec);
    if (ec) {
        // rename may fail across filesystems, fall back to copy
        fs::create_directories(dest_dir, ec);
        for (const auto& entry : fs::directory_iterator(temp_dir)) {
            fs::copy(entry.path(), fs::path(dest_dir) / entry.path().filename(),
                     fs::copy_options::overwrite_existing, ec);
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
        if (!dirEntry.is_directory())
            continue;
        std::string sub_dir      = dirEntry.path().string();
        std::string sub_dir_name = dirEntry.path().filename().string();

        // Validate: must have config.json
        if (!fs::exists(sub_dir + "/config.json")) {
            LOG_WARN("[ImportModel] Skipping directory without config.json: {}", sub_dir_name);
            continue;
        }

        // Must have a model file (extension depends on platform)
        bool has_model = fs::exists(sub_dir + "/model" + std::string(cosmo::util::kModelFileExt));
        if (!has_model) {
            has_model =
                std::any_of(fs::directory_iterator(sub_dir), fs::directory_iterator(), [](const auto& f) {
                    return f.path().extension() == cosmo::util::kModelFileExt ||
                           f.path().extension() == ".bmodel" || f.path().extension() == ".onnx";
                });
        }
        if (!has_model) {
            LOG_WARN("[ImportModel] Skipping directory without model file: {}", sub_dir_name);
            continue;
        }

        std::string dest_dir = (fs::path(models_dir) / sub_dir_name).string();

        // Remove existing if present
        if (fs::exists(dest_dir))
            fs::remove_all(dest_dir, ec);

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

    if (archivePath.empty() || !fs::exists(archivePath)) {
        LOG_WARN("[ImportModel] Archive file not found: {}", archivePath);
        return util::ErrorEnum::FileNotExist;
    }

    size_t archive_size = 0;
    try {
        archive_size = fs::file_size(archivePath);
    } catch (const std::exception& e) {
        LOG_WARN("[ImportModel] Failed to get archive size: {}", e.what());
    }
    LOG_INFO("[ImportModel] Starting import from: {}, size: {} bytes", archivePath, archive_size);

    // 1. Create temp extraction directory
    std::string timestamp = std::to_string(time(nullptr));
    std::string temp_dir  = cosmo::path::GetTemporaryDirPath() + "/model_import_" + timestamp;
    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
        LOG_WARN("[ImportModel] Failed to create temp directory: {}", temp_dir);
        return util::ErrorEnum::SysErr;
    }

    // 2. Extract archive (detect format: tar.gz or zip)
    std::string out_str;
    std::string extract_cmd;
    std::string lower_path = archivePath;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    if (lower_path.find(".zip") != std::string::npos) {
        extract_cmd =
            "unzip -o " + util::ShellEscape(archivePath) + " -d " + util::ShellEscape(temp_dir) + " 2>&1";
    } else {
        extract_cmd =
            "tar -xzf " + util::ShellEscape(archivePath) + " -C " + util::ShellEscape(temp_dir) + " 2>&1";
    }

    LOG_INFO("[ImportModel] Extracting: {}", extract_cmd);
    int extract_ret = util::Exec(extract_cmd, out_str);
    if (extract_ret != 0) {
        LOG_WARN("[ImportModel] Extract failed (ret={}): {}", extract_ret, out_str);
        fs::remove_all(temp_dir, ec);
        return util::ErrorEnum::SysErr;
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
            if (ext == cosmo::util::kModelFileExt || ext == ".bmodel" || ext == ".onnx") {
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
        fs::remove(archivePath);
    } catch (const std::exception& e) {
        LOG_WARN("[ImportModel] Failed to remove temp archive {}: {}", archivePath, e.what());
    }

    if (imported_count == 0) {
        LOG_WARN("{}", "[ImportModel] No valid model directories found in archive");
        return util::ErrorEnum::InvalidParam;
    }

    LOG_INFO("[ImportModel] Successfully imported {} model(s)", imported_count);
    return util::ErrorEnum::Success;
}

}  // namespace cosmo::service
