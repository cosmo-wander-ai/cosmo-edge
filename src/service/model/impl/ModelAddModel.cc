// ModelAddModel.cc — AddAtomicModel pipeline and helpers for ModelImportExporter.
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
#include <regex>
#include <sstream>

#include "infer/BmodelTool.h"
#include "nlohmann/json.hpp"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/FileUtil.h"
#include "util/PlatformConstants.h"

namespace cosmo::service {

// JSON helpers moved to ModelAddModel_Json.cc

// ============================================================
// Extracted helpers for AddAtomicModel (insert before AddAtomicModel)
// ============================================================

util::ErrorEnum ModelImportExporter::ValidateAddModelInputs(
    const std::string& modelCode, const std::string& modelName, const std::string& modelType,
    const std::vector<cosmo::Model::BmodelFileInfo>& bmodel_files, const std::string& vocabFilePath,
    const std::string& tokenizerFilePath, std::string& resolved_model_code,
    std::vector<std::string>& bmodel_paths) {
    namespace fs = std::filesystem;

    resolved_model_code = generate_unique_model_code_();
    LOG_INFO("[AddModel] Using resolved model code: {} (request code: {})", resolved_model_code, modelCode);
    if (resolved_model_code.empty())
        return util::ErrorEnum::SysErr;

    // 1. Validate model code format (7-digit number)
    if (resolved_model_code.length() != 7 ||
        !std::all_of(resolved_model_code.begin(), resolved_model_code.end(), ::isdigit)) {
        LOG_WARN("[AddModel] Generated model code is invalid: {}", resolved_model_code);
        return util::ErrorEnum::InvalidParam;
    }

    if (modelName.find('_') != std::string::npos) {
        LOG_WARN("{}", "[AddModel] Model name must not contain underscores");
        return util::ErrorEnum::InvalidParam;
    }

    // 2. Validate bmodel files
    if (bmodel_files.empty()) {
        LOG_WARN("{}", "[AddModel] No bmodel files provided");
        return util::ErrorEnum::InvalidParam;
    }

    bool is_sam2 = (modelType == "sam2");
    if (is_sam2 && bmodel_files.size() != 2) {
        LOG_WARN("{}", "[AddModel] SAM2 model requires exactly 2 bmodel files (encoder and decoder)");
        return util::ErrorEnum::InvalidParam;
    }
    if (!is_sam2 && bmodel_files.size() != 1) {
        LOG_WARN("{}", "[AddModel] Non-SAM2 model must have exactly 1 bmodel file");
        return util::ErrorEnum::InvalidParam;
    }

    if (modelType == "dino" && vocabFilePath.empty()) {
        LOG_WARN("{}", "[AddModel] DINO model requires a vocab.txt file");
        return util::ErrorEnum::InvalidParam;
    }
    if ((modelType == "qwen3vl" || modelType == "qwen3_5") && tokenizerFilePath.empty()) {
        LOG_WARN("{}", "[AddModel] This model type requires a tokenizer.json file");
        return util::ErrorEnum::InvalidParam;
    }

    // Check all bmodel files exist
    for (const auto& bmodelFile : bmodel_files) {
        if (!fs::exists(bmodelFile.filePath)) {
            LOG_WARN("[AddModel] bmodel file does not exist: {}", bmodelFile.filePath);
            return util::ErrorEnum::FileNotExist;
        }
        bmodel_paths.push_back(bmodelFile.filePath);
        LOG_INFO("[AddModel] bmodel file: role={}, path={}", bmodelFile.role, bmodelFile.filePath);
    }

    return util::ErrorEnum::Success;
}

util::ErrorEnum ModelImportExporter::CollectBmodelInfo(const std::string& modelType,
                                                       const std::vector<std::string>& bmodel_paths,
                                                       std::vector<cosmo::BmodelInfo>& bmodel_infos,
                                                       bool& use_template_defaults) {
    use_template_defaults = (modelType == "qwen3vl" || modelType == "qwen3_5");

    if (!use_template_defaults) {
        for (const auto& bmodelPath : bmodel_paths) {
            auto info = cosmo::BmodelTool::GetBmodelInfo(bmodelPath);
            if (!info.valid && info.error_msg != "SDK_NOT_AVAILABLE") {
                LOG_WARN("[AddModel] Failed to get bmodel info: {}", info.error_msg);
                return util::ErrorEnum::InvalidParam;
            }
            bmodel_infos.push_back(info);
        }

        if (!bmodel_infos.empty() && !bmodel_infos[0].valid &&
            bmodel_infos[0].error_msg == "SDK_NOT_AVAILABLE") {
            use_template_defaults = true;
            LOG_INFO("{}", "[AddModel] SDK not available, using template defaults");
        }

        for (size_t bi = 0; bi < bmodel_infos.size(); bi++) {
            std::string prefix = "[AddModel] bmodel[" + std::to_string(bi) + "]";
            cosmo::BmodelTool::LogBmodelInfo(bmodel_infos[bi], prefix);
        }
    }

    return util::ErrorEnum::Success;
}

std::string ModelImportExporter::CalculateNextVersion(const std::string& models_dir,
                                                      const std::string& modelCode) {
    namespace fs = std::filesystem;

    int max_version = 0;
    {
        std::string prefix = std::string(cosmo::util::kPlatformDirPrefix) + modelCode + "_";
        std::regex pattern("^" + prefix + ".+_V(\\d+)\\.0\\.(\\d+)$");
        for (const auto& dirEntry : fs::directory_iterator(models_dir)) {
            if (!dirEntry.is_directory())
                continue;
            std::string dir_name = dirEntry.path().filename().string();
            if (dir_name.find(prefix) == 0) {
                std::smatch matches;
                if (std::regex_match(dir_name, matches, pattern) && matches.size() > 2) {
                    try {
                        int major   = std::stoi(matches[1].str());
                        int minor   = std::stoi(matches[2].str());
                        int version = major * 1000 + minor;
                        if (version > max_version)
                            max_version = version;
                    } catch (const std::exception&) {
                    }
                }
            }
        }
    }

    int new_version = max_version + 1;
    int major       = (new_version - 1) / 1000 + 1;
    int minor       = (new_version - 1) % 1000;
    return "V" + std::to_string(major) + ".0." + std::to_string(minor);
}

util::ErrorEnum ModelImportExporter::WriteNnFile(const std::string& modelType,
                                                 const std::vector<std::string>& bmodel_paths,
                                                 const std::string& model_dir) {
    namespace fs = std::filesystem;

#ifdef COSMO_NN_USE_CPU_BACKEND
    // CPU/x86: copy .onnx file directly; no Sophon wrapper needed.
    std::string convert_error;
    if (modelType == "sam2") {
        const std::vector<std::string> dest_names = {"sam2_encoder.onnx", "sam2_decoder.onnx"};
        for (size_t i = 0; i < dest_names.size() && i < bmodel_paths.size(); i++) {
            std::error_code ec;
            std::string dest_path = (fs::path(model_dir) / dest_names[i]).string();
            fs::copy_file(bmodel_paths[i], dest_path, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                convert_error = "Failed to copy SAM2 ONNX file to " + dest_path + ": " + ec.message();
                break;
            }
            LOG_INFO("[AddModel] x86 SAM2: copied ONNX file to {}", dest_path);
        }
    } else {
        std::string model_file_path = model_dir + "/model.onnx";
        std::error_code ec;
        fs::copy_file(bmodel_paths[0], model_file_path, fs::copy_options::overwrite_existing, ec);
        if (ec)
            convert_error = "Failed to copy model file to " + model_file_path + ": " + ec.message();
        else
            LOG_INFO("[AddModel] x86: copied model file to {}", model_file_path);
    }
#else
    // Sophon: wrap bmodel(s) into .nn format
    std::string nn_path = model_dir + "/model.nn";

    std::string convert_error;
    if (modelType == "qwen3vl" || modelType == "qwen3_5") {
        std::error_code ec;
        fs::copy_file(bmodel_paths[0], nn_path, fs::copy_options::overwrite_existing, ec);
        if (ec)
            convert_error = "Failed to copy bmodel to model.nn: " + ec.message();
        else
            LOG_INFO("[AddModel] qwen3vl: copied raw bmodel to {} (no nn wrapper header)", nn_path);
    } else {
        convert_error = cosmo::BmodelTool::ConvertToNn(bmodel_paths, nn_path);
    }
#endif

    if (!convert_error.empty()) {
        try {
            fs::remove_all(model_dir);
        } catch (const std::exception&) {
        }
        LOG_WARN("[AddModel] model file conversion/copy failed: {}", convert_error);
        return util::ErrorEnum::SysErr;
    }
    return util::ErrorEnum::Success;
}

// UpdateTemplateConfig moved to ModelAddModel_Json.cc

util::ErrorEnum ModelImportExporter::CopyAuxiliaryFiles(const std::string& modelType,
                                                        const std::string& vocabFilePath,
                                                        const std::string& tokenizerFilePath,
                                                        const std::string& model_dir) {
    namespace fs = std::filesystem;

    if (modelType == "dino" && !vocabFilePath.empty()) {
        if (fs::exists(vocabFilePath)) {
            std::string dest_vocab = model_dir + "/vocab.txt";
            try {
                fs::copy_file(vocabFilePath, dest_vocab, fs::copy_options::overwrite_existing);
                LOG_INFO("[AddModel] Copied vocab.txt to {}", dest_vocab);
            } catch (const std::exception& e) {
                try {
                    fs::remove_all(model_dir);
                } catch (const std::exception&) {
                }
                LOG_WARN("[AddModel] Failed to copy vocab.txt: {}", e.what());
                return util::ErrorEnum::SysErr;
            }
        } else {
            try {
                fs::remove_all(model_dir);
            } catch (const std::exception&) {
            }
            LOG_WARN("[AddModel] vocab.txt temp file does not exist: {}", vocabFilePath);
            return util::ErrorEnum::FileNotExist;
        }
    }

    if ((modelType == "qwen3vl" || modelType == "qwen3_5") && !tokenizerFilePath.empty()) {
        if (fs::exists(tokenizerFilePath)) {
            std::string dest_tokenizer = model_dir + "/tokenizer.json";
            try {
                fs::copy_file(tokenizerFilePath, dest_tokenizer, fs::copy_options::overwrite_existing);
                LOG_INFO("[AddModel] Copied tokenizer.json to {}", dest_tokenizer);
            } catch (const std::exception& e) {
                try {
                    fs::remove_all(model_dir);
                } catch (const std::exception&) {
                }
                LOG_WARN("[AddModel] Failed to copy tokenizer.json: {}", e.what());
                return util::ErrorEnum::SysErr;
            }
        } else {
            try {
                fs::remove_all(model_dir);
            } catch (const std::exception&) {
            }
            LOG_WARN("[AddModel] tokenizer.json temp file does not exist: {}", tokenizerFilePath);
            return util::ErrorEnum::FileNotExist;
        }
    }

    return util::ErrorEnum::Success;
}

// ============================================================
// Slim AddAtomicModel orchestrator
// ============================================================
util::ErrorEnum ModelImportExporter::AddAtomicModel(
    const std::string& modelCode, const std::string& modelName, const std::string& modelType,
    const std::string& description, const std::vector<cosmo::Model::BmodelFileInfo>& bmodel_files,
    const std::string& vocabFilePath, const std::string& tokenizerFilePath,
    const std::string& normalizationMode, const std::string& colorChannel) {
    namespace fs = std::filesystem;

    const std::string models_dir   = get_model_path_();
    const std::string template_dir = get_model_template_path_();

    // Collect temp files for cleanup
    std::vector<std::string> temp_files_to_cleanup;
    for (const auto& bmodelFile : bmodel_files) {
        if (!bmodelFile.filePath.empty())
            temp_files_to_cleanup.push_back(bmodelFile.filePath);
    }
    if (!vocabFilePath.empty())
        temp_files_to_cleanup.push_back(vocabFilePath);
    if (!tokenizerFilePath.empty())
        temp_files_to_cleanup.push_back(tokenizerFilePath);

    auto cleanup_and_return = [&](util::ErrorEnum err) -> util::ErrorEnum {
        cosmo::BmodelTool::CleanupTempFiles(temp_files_to_cleanup);
        return err;
    };

    // 1-2. Validate inputs
    std::string resolved_model_code;
    std::vector<std::string> bmodel_paths;
    auto err = ValidateAddModelInputs(modelCode, modelName, modelType, bmodel_files, vocabFilePath,
                                      tokenizerFilePath, resolved_model_code, bmodel_paths);
    if (err != util::ErrorEnum::Success)
        return cleanup_and_return(err);

    // 3. Read template file
    std::string template_path = (fs::path(template_dir) / (modelType + ".json")).string();
    if (!fs::exists(template_path)) {
        LOG_WARN("[AddModel] Model type template not found: {}", template_path);
        return cleanup_and_return(util::ErrorEnum::FileNotExist);
    }

    std::ifstream templateFile(template_path);
    if (!templateFile.is_open()) {
        LOG_WARN("[AddModel] Failed to open template file: {}", template_path);
        return cleanup_and_return(util::ErrorEnum::SysErr);
    }

    std::stringstream templateBuffer;
    templateBuffer << templateFile.rdbuf();
    templateFile.close();

    nlohmann::json templateDoc;
    try {
        templateDoc = nlohmann::json::parse(templateBuffer.str());
    } catch (const std::exception& e) {
        LOG_WARN("[AddModel] Template file JSON parse error: {} ({})", template_path, e.what());
        return cleanup_and_return(util::ErrorEnum::InvalidParam);
    }

    // 4. Get bmodel info
    bool use_template_defaults = false;
    std::vector<cosmo::BmodelInfo> bmodel_infos;
    err = CollectBmodelInfo(modelType, bmodel_paths, bmodel_infos, use_template_defaults);
    if (err != util::ErrorEnum::Success)
        return cleanup_and_return(err);

    // 5. Calculate version number
    std::string version_str = CalculateNextVersion(models_dir, resolved_model_code);

    // 6. Create model directory
    std::string clean_model_name = modelName;
    std::replace(clean_model_name.begin(), clean_model_name.end(), ' ', '_');
    std::replace(clean_model_name.begin(), clean_model_name.end(), '/', '_');
    std::replace(clean_model_name.begin(), clean_model_name.end(), '\\', '_');

    std::string folder_name = std::string(cosmo::util::kPlatformDirPrefix) + resolved_model_code + "_" +
                              clean_model_name + "_" + version_str;
    std::string model_dir = (fs::path(models_dir) / folder_name).string();

    LOG_INFO("[AddModel] Creating model directory: {}", model_dir);
    if (!util::CreateDir(model_dir)) {
        LOG_WARN("[AddModel] Failed to create model directory: {}", model_dir);
        return cleanup_and_return(util::ErrorEnum::SysErr);
    }

    // 7. Write model.nn
    err = WriteNnFile(modelType, bmodel_paths, model_dir);
    if (err != util::ErrorEnum::Success)
        return cleanup_and_return(err);

    // 8. Update template config
    UpdateTemplateConfig(templateDoc, resolved_model_code, version_str, modelName, modelType, description,
                         bmodel_infos, use_template_defaults, normalizationMode, colorChannel);

    // 8.1 Validate model output format
    {
        try {
            validate_model_output_format_(templateDoc);
        } catch (const std::exception&) {
            try {
                fs::remove_all(model_dir);
            } catch (const std::exception&) {
            }
            cosmo::BmodelTool::CleanupTempFiles(temp_files_to_cleanup);
            throw;  // re-throw to preserve original exception behavior
        }
    }

    // 9. Save config.json
    std::string config_path  = model_dir + "/config.json";
    std::string json_content = templateDoc.dump(2);

    if (!util::WriteFile(config_path, json_content)) {
        try {
            fs::remove_all(model_dir);
        } catch (const std::exception&) {
        }
        LOG_WARN("[AddModel] Failed to save config.json: {}", config_path);
        return cleanup_and_return(util::ErrorEnum::SysErr);
    }

    // 9.1 Copy auxiliary files
    err = CopyAuxiliaryFiles(modelType, vocabFilePath, tokenizerFilePath, model_dir);
    if (err != util::ErrorEnum::Success)
        return cleanup_and_return(err);

    // 10. Clean up temp files
    cosmo::BmodelTool::CleanupTempFiles(temp_files_to_cleanup);

    // 11. Update memory mapping so service can find it without restart
    if (set_model_path_mapping_) {
        set_model_path_mapping_(resolved_model_code, model_dir);
    }

    LOG_INFO("[AddModel] Successfully added model: code={}, name={}, version={}, directory={}",
             resolved_model_code, modelName, version_str, model_dir);

    return util::ErrorEnum::Success;
}

// ImportFlatArchive, ImportDirectoryArchive, ImportModel — moved to ModelImporter.cc

}  // namespace cosmo::service
