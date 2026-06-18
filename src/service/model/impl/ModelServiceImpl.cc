// clang-format off
#include "service/detail/ServiceRegistry.h"
#include "service/model/impl/ModelServiceImpl.h"
// clang-format on

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <unordered_set>

#include "service/model/impl/ModelConfigParser.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/Exec.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/PathUtil.h"
#include "util/PlatformConstants.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace cosmo::service {

struct ModelServiceImpl::ModelJsonOutputInfo {
    std::string label;
    std::string class_name;
    std::vector<float> threshold;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ModelJsonOutputInfo, label, class_name, threshold)
};
struct ModelServiceImpl::ModelJsonInstruction {
    std::string output_node;
    std::vector<int> shape;
    std::vector<ModelJsonOutputInfo> output_info;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ModelJsonInstruction, output_node, shape, output_info)
};

struct ModelServiceImpl::ModelJsonConfig {
    std::vector<ModelJsonInstruction> instruction;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ModelJsonConfig, instruction)
};

struct ModelServiceImpl::ModelLabel {
    std::string name_cn;
    std::string class_name;
    std::string label;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ModelLabel, name_cn, class_name, label)
};

struct ModelServiceImpl::ModelJsonInfo {
    std::string type;
    std::string chip_type;
    std::string algorithm_code;
    std::string version;
    std::string name;
    std::vector<ModelLabel> labels;
    ModelJsonConfig config;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ModelJsonInfo, type, chip_type, algorithm_code, version,
                                                config)
};

struct ModelServiceImpl::ModelLabelInfo {
    std::string code;
    std::string name;
    std::string label;
    std::string model_type;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ModelLabelInfo, code, name, label, model_type)
};

// ──────────────────────────────────────────────
// Path retrieval
// ──────────────────────────────────────────────

std::string ModelServiceImpl::GetModelPath() {
    return cosmo::path::GetModelPath();
}

std::string ModelServiceImpl::GetModelTemplatePath() {
    return cosmo::path::GetModelTemplatePath();
}

std::string ModelServiceImpl::GetModelComponentsJsonPath() {
    return cosmo::path::GetModelComponentsJsonPath();
}

std::vector<std::string> ModelServiceImpl::GetModelSearchPaths() {
    return cosmo::path::GetModelSearchPaths();
}

void ModelServiceImpl::Init() {
    import_exporter_.SetHelpers(
        [this]() { return GetModelPath(); }, [this]() { return GetModelTemplatePath(); },
        [this](const std::string& code) { return FindModelDir(code); },
        [this]() { return GenerateUniqueModelCode(); },
        [this](const nlohmann::json& doc) { ValidateModelOutputFormat(doc); },
        [this](const std::string& code, const std::string& path) { SetModelPathMapping(code, path); });

    // Scan models directory to build path mappings at startup
    namespace fs = std::filesystem;
    for (const auto& modelsDir : GetModelSearchPaths()) {
        std::error_code ec;
        for (const auto& dirEntry : fs::directory_iterator(modelsDir, ec)) {
            if (!dirEntry.is_directory())
                continue;
            auto cfg = detail::ModelConfigParser::Parse((dirEntry.path() / "config.json").string());
            if (cfg.valid && !cfg.algorithm_code.empty() && GetModelPathMapping(cfg.algorithm_code).empty()) {
                SetModelPathMapping(cfg.algorithm_code, dirEntry.path().string());
            }
        }
        if (ec) {
            LOG_WARN("Failed to scan models directory: {}", modelsDir);
        }
    }
    LOG_INFO("{}", "ModelServiceImpl Init (disk-based, no model.json)");
}

std::string ModelServiceImpl::UpzipModelFile(std::string filePath) {
    std::string upload_path = cosmo::util::RemoveExtension(filePath);
    std::string cmd =
        "unzip -q -d " + cosmo::util::ShellEscape(upload_path) + " " + cosmo::util::ShellEscape(filePath);
    std::string out_str;
    auto ret = cosmo::util::Exec(cmd, out_str);
    if (ret != 0) {
        LOG_WARN("{} Failed Result:{}", cmd, out_str);
        return "";
    }
    LOG_INFO("{} OK", cmd);
    return upload_path;
}

cosmo::util::ErrorEnum ModelServiceImpl::CheckModelValid(std::string un_zip_file, ModelJsonInfo& cfgInfo) {
    auto file_name = cosmo::util::GetFileName(un_zip_file);
    LOG_INFO("{} Get:{}", un_zip_file, file_name);
    auto keys = cosmo::util::Split(file_name, "_");
    if (keys.size() != 5) {
        LOG_WARN("path:{}{} split To {}, It's Not a Right Model", un_zip_file, file_name, keys.size());
        return cosmo::util::ErrorEnum::ModelFileName;
    }
    auto plat_form         = keys.at(1);
    auto id                = keys.at(2);
    auto name              = keys.at(3);
    auto version           = keys.at(4);
    cfgInfo.name           = name;
    cfgInfo.algorithm_code = id;

    LOG_INFO("{} -> {}/{}/{}/{}", file_name, plat_form, id, name, version);

    std::string filter;
    auto files = cosmo::util::GetAllFileName(un_zip_file, filter);
    if (files.empty()) {
        LOG_WARN("{} No Model File", un_zip_file);
        return cosmo::util::ErrorEnum::ModelFileLack;
    }

    bool found_model  = false;
    bool found_json   = false;
    bool decoded_json = false;

    for (const auto& file : files) {
        if (file.find(cosmo::util::kModelFileExt) != std::string::npos ||
            file.find(".onnx") != std::string::npos) {
            found_model = true;
        }
        if (file.find(".json") != std::string::npos) {
            found_json     = true;
            auto json_file = (std::filesystem::path(un_zip_file) / file).string();
            auto content   = cosmo::util::ReadFile(json_file);
            if (cosmo::util::DecodeJson(content, cfgInfo)) {
                decoded_json = true;
            }
        }

        if (file.find(".label") != std::string::npos) {
            auto json_file = (std::filesystem::path(un_zip_file) / file).string();
            auto content   = cosmo::util::ReadFile(json_file);
            ModelLabelInfo labelInfo;
            if (cosmo::util::DecodeJson(content, labelInfo)) {
                cfgInfo.name = labelInfo.name;
                (void)cosmo::util::DecodeJson(labelInfo.label, cfgInfo.labels);  // best-effort
                LOG_INFO("cfgInfo.labelInfos:{}", labelInfo.label);
            }
        }
    }

    if ((found_model == false) || (found_json == false)) {
        return cosmo::util::ErrorEnum::ModelFileLack;
    }
    if (decoded_json == false) {
        return cosmo::util::ErrorEnum::ModelFileAnalysis;
    }
    if (cosmo::util::ToLower(cfgInfo.chip_type) != cosmo::util::ToLower(plat_form)) {
        LOG_WARN("{} ChipType:{} platFrom:{} Not Match {} {}", un_zip_file, cfgInfo.chip_type, plat_form,
                 cosmo::util::ToLower(cfgInfo.chip_type), cosmo::util::ToLower(plat_form));
        return cosmo::util::ErrorEnum::ModelFilePlatform;
    }
    cfgInfo.version = version;

    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum ModelServiceImpl::ModelAdd(const std::string& filePath) {
    auto un_zip_file = UpzipModelFile(filePath);
    if (un_zip_file.empty()) {
        LOG_INFO("Unzip {} Failed", filePath);
        return cosmo::util::ErrorEnum::UnZipFileFailed;
    }
    ModelJsonInfo modelInfo;
    auto ret = CheckModelValid(un_zip_file, modelInfo);
    if (cosmo::util::ErrorEnum::Success != ret) {
        return ret;
    }

    // Move the unzipped model to the models directory
    auto temp = cosmo::util::FileMove(un_zip_file, GetModelPath());
    if (!temp) {
        LOG_WARN("Move {} to file {} Failed", un_zip_file, GetModelPath());
        return cosmo::util::ErrorEnum::FileMoveFailed;
    }
    auto model_path =
        (std::filesystem::path(GetModelPath()) / cosmo::util::GetFileName(un_zip_file)).string();
    SetModelPathMapping(modelInfo.algorithm_code, model_path);

    LOG_INFO("ModelAdd {} -> {} Success", filePath, model_path);
    return cosmo::util::ErrorEnum::Success;
}

// ── Disk-based helper: convert ParsedModelConfig to cosmo::ModelInfo ──

namespace {
    cosmo::ModelInfo ParsedConfigToModelInfo(const detail::ParsedModelConfig& cfg,
                                             const std::string& model_dir) {
        cosmo::ModelInfo info;
        info.id      = cfg.algorithm_code;
        info.name    = cfg.model_name;
        info.version = detail::ModelConfigParser::ParseVersionFromDirName(
            std::filesystem::path(model_dir).filename().string(), cfg.version);
        info.path = model_dir;

        for (const auto& pl : cfg.labels) {
            cosmo::ModelLabel label;
            label.code      = pl.class_name;
            label.labelName = pl.class_name;
            label.label     = pl.id;
            if (pl.thresholds.size() >= 2) {
                label.confidenceHigh = pl.thresholds[0];
                label.confidence     = pl.thresholds[1];
            } else if (pl.thresholds.size() >= 1) {
                label.confidenceHigh = pl.thresholds[0];
                label.confidence     = pl.thresholds[0];
            }
            info.labels.push_back(label);
        }
        return info;
    }
}  // namespace

cosmo::ModelInfo ModelServiceImpl::GetModelInfo(const std::string& modelCode) {
    std::string model_dir = FindModelDir(modelCode);
    if (model_dir.empty()) {
        return {};
    }
    auto cfg = detail::ModelConfigParser::Parse((std::filesystem::path(model_dir) / "config.json").string());
    if (!cfg.valid) {
        return {};
    }
    return ParsedConfigToModelInfo(cfg, model_dir);
}

std::vector<cosmo::ModelInfo> ModelServiceImpl::QueryModelInfo(const std::string& modelName,
                                                               const std::string& modelCode, int page_num,
                                                               int page_size, size_t& total) {
    namespace fs = std::filesystem;
    std::vector<cosmo::ModelInfo> all_models;
    std::error_code ec;
    std::unordered_set<std::string> seenModelCodes;

    for (const auto& modelsDir : GetModelSearchPaths()) {
        ec.clear();
        for (const auto& dirEntry : fs::directory_iterator(modelsDir, ec)) {
            if (!dirEntry.is_directory())
                continue;
            auto cfg = detail::ModelConfigParser::Parse((dirEntry.path() / "config.json").string());
            if (!cfg.valid || cfg.algorithm_code.empty() || seenModelCodes.count(cfg.algorithm_code))
                continue;
            seenModelCodes.insert(cfg.algorithm_code);
            auto info = ParsedConfigToModelInfo(cfg, dirEntry.path().string());
            // Apply filters
            if (!modelName.empty() && info.name.find(modelName) == std::string::npos)
                continue;
            if (!modelCode.empty() && info.id.find(modelCode) == std::string::npos)
                continue;
            all_models.push_back(std::move(info));
        }
        if (ec) {
            LOG_WARN("Failed to iterate models directory: {}", modelsDir);
        }
    }

    total = all_models.size();
    if (page_num <= 0 || page_size <= 0)
        return {};

    size_t start_idx = static_cast<size_t>((page_num - 1) * page_size);
    if (start_idx >= all_models.size())
        return {};

    size_t end_idx = std::min(start_idx + static_cast<size_t>(page_size), all_models.size());
    return {all_models.begin() + start_idx, all_models.begin() + end_idx};
}

bool ModelServiceImpl::ModelValid(const std::string& modelCode, std::string& modelName) {
    std::string model_dir = FindModelDir(modelCode);
    if (model_dir.empty()) {
        modelName = modelCode;
        return false;
    }
    auto cfg = detail::ModelConfigParser::Parse((std::filesystem::path(model_dir) / "config.json").string());
    if (!cfg.valid) {
        modelName = modelCode;
        return false;
    }
    modelName = cfg.model_name;
    return true;
}

bool ModelServiceImpl::ModelValid(const std::string& modelCode) {
    std::string modelName;
    return ModelValid(modelCode, modelName);
}

// ──────────────────────────────────────────────
// Private helpers
// ──────────────────────────────────────────────

std::string ModelServiceImpl::FindModelDir(const std::string& modelCode) {
    namespace fs = std::filesystem;
    std::string result;
    std::string prefix = std::string(cosmo::util::kPlatformDirPrefix) + modelCode + "_";

    for (const auto& modelsDir : GetModelSearchPaths()) {
        std::error_code ec;
        for (const auto& dirEntry : fs::directory_iterator(modelsDir, ec)) {
            if (!dirEntry.is_directory())
                continue;
            std::string dir_name = dirEntry.path().filename().string();
            if (dir_name.find(prefix) == 0) {
                result = dirEntry.path().string();
                break;
            }
        }
        if (!result.empty())
            break;
    }
    return result;
}

// Crud operations — moved to ModelServiceCrud.cc

}  // namespace cosmo::service
