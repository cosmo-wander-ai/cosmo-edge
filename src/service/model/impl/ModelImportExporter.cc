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
#include "util/Exec.h"
#include "util/PathUtil.h"

namespace cosmo::service {

// Anonymous namespace helpers (IsDetectionModel, InferClassCount, etc.) — moved to ModelAddModel.cc

void ModelImportExporter::SetHelpers(
    std::function<std::string()> getModelPath, std::function<std::string()> getModelTemplatePath,
    std::function<std::string(const std::string&)> findModelDir,
    std::function<std::string()> generateUniqueModelCode,
    std::function<void(const nlohmann::json&)> validateModelOutputFormat,
    std::function<void(const std::string&, const std::string&)> setModelPathMapping) {
    get_model_path_               = std::move(getModelPath);
    get_model_template_path_      = std::move(getModelTemplatePath);
    find_model_dir_               = std::move(findModelDir);
    generate_unique_model_code_   = std::move(generateUniqueModelCode);
    validate_model_output_format_ = std::move(validateModelOutputFormat);
    set_model_path_mapping_       = std::move(setModelPathMapping);
}

util::ErrorEnum ModelImportExporter::ExportModelConfig(const std::string& modelCode,
                                                       const std::string& modelName, std::string& outFilePath,
                                                       std::string& outFileName) {
    if (modelCode.empty()) {
        LOG_WARN("{}", "modelCode is empty in exportConfig request");
        return util::ErrorEnum::InvalidParam;
    }

    // Find model directory (need dirName for tar)
    namespace fs               = std::filesystem;
    std::string model_dir_path = find_model_dir_(modelCode);
    std::string model_dir_name;
    if (!model_dir_path.empty()) {
        model_dir_name = fs::path(model_dir_path).filename().string();
    }

    if (model_dir_path.empty()) {
        LOG_WARN("Model directory not found for modelCode: {}", modelCode);
        return util::ErrorEnum::FileNotExist;
    }
    const std::string models_dir = fs::path(model_dir_path).parent_path().string();

    // Clean model name for filename
    std::string clean_model_name = modelName;
    std::replace_if(
        clean_model_name.begin(), clean_model_name.end(),
        [](char c) {
            return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                   c == '>' || c == '|';
        },
        '_');
    if (clean_model_name.empty())
        clean_model_name = "model";

    // Create tar.gz
    std::string timestamp = std::to_string(time(nullptr));
    std::string archive_file_path =
        cosmo::path::GetTemporaryDirPath() + "/model_export_" + modelCode + "_" + timestamp + ".tar.gz";

    std::string tar_command = "cd " + util::ShellEscape(models_dir) + " && tar -czf " +
                              util::ShellEscape(archive_file_path) + " " + util::ShellEscape(model_dir_name) +
                              " > /dev/null 2>&1";

    LOG_INFO("Executing tar command: {}", tar_command);

    std::string out_str;
    int ret = util::Exec(tar_command, out_str);
    if (ret != 0) {
        LOG_WARN("Failed to create tar.gz file, command return code: {}, error: {}", ret, out_str);
        return util::ErrorEnum::SysErr;
    }

    if (!fs::exists(archive_file_path) || !fs::is_regular_file(archive_file_path)) {
        LOG_WARN("Tar.gz file was not created successfully: {}", archive_file_path);
        return util::ErrorEnum::SysErr;
    }

    size_t file_size = fs::file_size(archive_file_path);
    LOG_INFO("Successfully created tar.gz file for modelCode: {}, path: {}, size: {} bytes", modelCode,
             archive_file_path, file_size);

    outFileName = clean_model_name + "_" + modelCode + ".tar.gz";
    outFilePath = archive_file_path;

    return util::ErrorEnum::Success;
}

// ValidateAddModelInputs through AddAtomicModel — moved to ModelAddModel.cc
// ImportFlatArchive, ImportDirectoryArchive, ImportModel — moved to ModelImporter.cc

}  // namespace cosmo::service
