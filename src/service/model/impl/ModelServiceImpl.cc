// clang-format off
#include "service/detail/ServiceRegistry.h"
#include "service/model/impl/ModelServiceImpl.h"
// clang-format on

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "service/model/impl/ModelConfigParser.h"
#include "util/ArchiveListingValidator.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/Exec.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/LimitedTypeJson.h"
#include "util/NnBackendConstants.h"
#include "util/PathUtil.h"
#include "util/ResourceBudget.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace cosmo::service {

namespace {

    constexpr size_t kMaxLegacyModelArchiveEntries = 10000;

    bool InspectZipMemberPaths(const std::string& archive_path, const std::string& extraction_root,
                               cosmo::util::ArchiveListingInspection& inspection) {
        if (!cosmo::util::InspectArchiveListingFile(archive_path,
                                                    cosmo::util::ArchiveListingFormat::kZipVerbose,
                                                    kMaxLegacyModelArchiveEntries, inspection) ||
            inspection.total_bytes == 0) {
            return false;
        }
        const auto budget = cosmo::util::InspectStorageResourceBudget(extraction_root);
        if (!budget.valid) {
            throw cosmo::util::ErrorMessage(cosmo::util::ErrorEnum::SysErr,
                                            "Cannot inspect legacy model extraction storage");
        }
        if (inspection.total_bytes > budget.usable_bytes) {
            throw cosmo::util::ResourceLimitError(
                "Insufficient safe disk space to extract the model archive", "archive-extraction",
                "model-archive", inspection.total_bytes, budget.usable_bytes, budget.reserve_bytes);
        }
        return true;
    }

    bool ValidateExtractedModelTree(const std::string& root,
                                    const cosmo::util::ArchiveListingInspection& inspection) {
        return cosmo::path::ValidateDirectoryTreeWithinRoot(
            root, kMaxLegacyModelArchiveEntries, inspection.largest_file_bytes, inspection.total_bytes);
    }

    bool IsZipFile(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        std::array<unsigned char, 4> header{};
        if (!stream.read(reinterpret_cast<char*>(header.data()), header.size())) {
            return false;
        }
        return header[0] == 'P' && header[1] == 'K' &&
               ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
                (header[2] == 7 && header[3] == 8));
    }

}  // namespace

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
    namespace fs                  = std::filesystem;
    const std::string preset_root = cosmo::path::GetPresetModelPath();
    for (const auto& modelsDir : GetModelSearchPaths()) {
        const bool is_preset_dir = cosmo::path::IsWithinRoot(preset_root, modelsDir);
        std::error_code ec;
        for (const auto& dirEntry : fs::directory_iterator(modelsDir, ec)) {
            if (!dirEntry.is_directory())
                continue;
            const fs::path cfg_path = dirEntry.path() / "config.json";
            auto cfg                = detail::ModelConfigParser::Parse(cfg_path.string());
            if (cfg.valid && !cfg.algorithm_code.empty() && GetModelPathMapping(cfg.algorithm_code).empty()) {
                SetModelPathMapping(cfg.algorithm_code, dirEntry.path().string());
            }
            // Preset models: snapshot the factory config so "restore defaults" can revert user edits.
            if (is_preset_dir) {
                const fs::path default_cfg = dirEntry.path() / "config.default.json";
                std::error_code cp_ec;
                if (!fs::exists(default_cfg, cp_ec)) {
                    fs::copy_file(cfg_path, default_cfg, fs::copy_options::skip_existing, cp_ec);
                }
            }
        }
        if (ec) {
            LOG_WARN("Failed to scan models directory: {}", modelsDir);
        }
    }
    LOG_INFO("{}", "ModelServiceImpl Init (disk-based, no model.json)");
}

std::string ModelServiceImpl::UpzipModelFile(std::string filePath) {
    namespace fs           = std::filesystem;
    const auto upload_root = cosmo::path::GetUploadPath();
    std::string managed_archive;
    if (!cosmo::path::ResolveExistingPathWithinRoot(
            upload_root, filePath, cosmo::path::PathEntryType::kRegularFile, managed_archive)) {
        LOG_WARN("{}", "Reject unmanaged or unsafe legacy model archive");
        return {};
    }
    std::error_code archive_ec;
    const auto archive_size = fs::file_size(managed_archive, archive_ec);
    cosmo::util::ArchiveListingInspection inspection;
    if (archive_ec || archive_size == 0 || !IsZipFile(managed_archive) ||
        !InspectZipMemberPaths(managed_archive, upload_root, inspection)) {
        LOG_WARN("{}", "Reject invalid legacy model archive");
        return {};
    }

    std::string upload_path;
    if (!cosmo::path::ResolvePathWithinRoot(upload_root, cosmo::util::RemoveExtension(managed_archive),
                                            upload_path)) {
        return {};
    }
    std::error_code ec;
    const auto status = fs::symlink_status(upload_path, ec);
    if ((!ec && fs::exists(status)) || fs::is_symlink(status)) {
        LOG_WARN("Legacy model extraction destination already exists: {}", upload_path);
        return {};
    }

    std::vector<std::string> argv{"unzip", "-q", "-d", upload_path, managed_archive};
    std::string out_str;
    auto ret = cosmo::util::Exec(argv, out_str);
    if (ret != 0) {
        LOG_WARN("unzip legacy model into {} failed: {}", upload_path, out_str);
        fs::remove_all(upload_path, ec);
        return "";
    }
    if (!ValidateExtractedModelTree(upload_path, inspection)) {
        LOG_WARN("Legacy model archive extracted unsafe entries into {}", upload_path);
        fs::remove_all(upload_path, ec);
        return "";
    }
    LOG_INFO("Unpacked managed legacy model archive into {}", upload_path);
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
#ifdef COSMO_NN_USE_AXERA_BACKEND
        if (std::filesystem::path(file).extension() == cosmo::util::kModelFileExt) {
#else
        if (file.find(cosmo::util::kModelFileExt) != std::string::npos ||
            file.find(".onnx") != std::string::npos || file.find(".rknn") != std::string::npos) {
#endif
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
    // config.json "chip_type" is the source of truth; validate it against the chip
    // set supported by the compiled backend. The folder-name token is a label, not a gate.
    if (!cosmo::util::IsSupportedChip(cfgInfo.chip_type)) {
        LOG_WARN("{} Unsupported ChipType:{}", un_zip_file, cfgInfo.chip_type);
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
    const auto destination_name = cosmo::util::GetFileName(un_zip_file);
    std::string model_path;
    if (!cosmo::path::IsSafePathComponent(destination_name, 200) ||
        !cosmo::path::ResolvePathWithinRoot(
            GetModelPath(), (std::filesystem::path(GetModelPath()) / destination_name).string(),
            model_path)) {
        LOG_WARN("Reject unsafe legacy model destination: {}", destination_name);
        return cosmo::util::ErrorEnum::InvalidParam;
    }
    std::error_code destination_ec;
    const auto destination_status = std::filesystem::symlink_status(model_path, destination_ec);
    if (!destination_ec && std::filesystem::exists(destination_status)) {
        LOG_WARN("Refusing to merge legacy model into existing destination: {}", model_path);
        return cosmo::util::ErrorEnum::ExistedName;
    }
    auto temp = cosmo::util::FileMove(un_zip_file, GetModelPath());
    if (!temp) {
        LOG_WARN("Move {} to file {} Failed", un_zip_file, GetModelPath());
        return cosmo::util::ErrorEnum::FileMoveFailed;
    }
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

    // Chip-agnostic: folder is prod_<TOKEN>_<CODE>_<NAME>_<VERSION> where TOKEN may be
    // BM1688, CV186X, SOPHGO, ... Locate by algorithm code (matched literally after the
    // token, so codes containing underscores are handled correctly).
    std::regex pattern("^prod_[A-Z0-9]+_" + modelCode + "_");

    for (const auto& modelsDir : GetModelSearchPaths()) {
        std::error_code ec;
        for (const auto& dirEntry : fs::directory_iterator(modelsDir, ec)) {
            if (!dirEntry.is_directory())
                continue;
            std::string dir_name = dirEntry.path().filename().string();
            if (std::regex_search(dir_name, pattern)) {
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
