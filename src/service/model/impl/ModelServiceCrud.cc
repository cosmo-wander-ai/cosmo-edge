// ModelServiceCrud.cc — Crud operations for ModelServiceImpl.
// Split from ModelServiceImpl.cc to reduce file size (DEBT-007).

// clang-format off
#include "service/detail/ServiceRegistry.h"
#include "service/model/impl/ModelServiceImpl.h"
// clang-format on

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "service/algorithm/IAlgorithmQuery.h"
#include "service/camera/ICameraTaskConfig.h"
#include "service/model/impl/ModelConfigParser.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"
#include "util/Exec.h"
#include "util/JsonFileUtil.h"
#include "util/JsonStructUtil.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"

namespace cosmo::service {

std::string ModelServiceImpl::GenerateUniqueModelCode() {
    namespace chrono = std::chrono;

    constexpr int kMinCode  = 1000000;
    constexpr int kCodeSpan = 9000000;
    auto now_ms =
        chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    int start_offset = static_cast<int>(now_ms % kCodeSpan);

    for (int i = 0; i < kCodeSpan; ++i) {
        const int numeric_code = kMinCode + ((start_offset + i) % kCodeSpan);
        const std::string code = std::to_string(numeric_code);
        if (FindModelDir(code).empty())
            return code;
    }

    return "";
}

void ModelServiceImpl::ValidateModelOutputFormat(const nlohmann::json& doc) {
    std::string model_type;
    if (doc.contains("model_type") && doc["model_type"].is_string())
        model_type = doc["model_type"].get<std::string>();

    if (!doc.contains("models") || !doc["models"].is_array() || doc["models"].empty())
        return;

    const auto& firstModel = doc["models"][0];
    if (!firstModel.contains("outputs") || !firstModel["outputs"].is_array())
        return;
    const auto& outputs = firstModel["outputs"];
    if (outputs.empty())
        return;

    auto get_shape = [](const nlohmann::json& out) -> std::vector<int> {
        std::vector<int> s;
        if (!out.contains("shape") || !out["shape"].is_array())
            return s;
        for (const auto& v : out["shape"])
            s.push_back(v.get<int>());
        return s;
    };
    auto getYoloV5RawBoxCount = [&firstModel]() -> int {
        if (!firstModel.contains("params") || !firstModel["params"].is_object())
            return 0;
        const auto& params = firstModel["params"];
        if (!params.contains("input_size") || !params["input_size"].is_array() ||
            params["input_size"].size() < 2)
            return 0;
        if (!params["input_size"][0].is_number_integer() || !params["input_size"][1].is_number_integer())
            return 0;

        const int height = params["input_size"][0].get<int>();
        const int width  = params["input_size"][1].get<int>();
        if (height <= 0 || width <= 0)
            return 0;

        int count = 0;
        for (int stride : {8, 16, 32}) {
            if (height % stride != 0 || width % stride != 0)
                return 0;
            count += 3 * (height / stride) * (width / stride);
        }
        return count;
    };
    if (model_type == "yolov5_det") {
        if (outputs.size() == 1) {
            std::vector<int> shape = get_shape(outputs[0]);
            int rawBoxCount        = getYoloV5RawBoxCount();
            bool isRawOneClassYolo = shape.size() == 3 && shape[1] > 0 && shape[2] == 6 && rawBoxCount > 0 &&
                                     shape[1] == rawBoxCount;
            if (shape.size() == 3 && shape[1] > 0 && shape[2] == 6 && !isRawOneClassYolo) {
                throw cosmo::util::ErrorMessage(
                    cosmo::util::make_error_condition(cosmo::util::ErrorEnum::ParameterException),
                    "请导出不带 decode 的模型（Detect 层输出特征图）。添加不成功");
            }
        }
    } else if (model_type == "yolov8_det" || model_type == "yolov9_det" || model_type == "yolov11_det" ||
               model_type == "yolov12_det") {
        bool reject = (outputs.size() > 1);
        if (!reject) {
            for (const auto& out : outputs) {
                std::vector<int> shape = get_shape(out);
                if (shape.size() < 3 || shape.size() == 4) {
                    reject = true;
                    break;
                }
            }
        }
        if (reject) {
            throw cosmo::util::ErrorMessage(
                cosmo::util::make_error_condition(cosmo::util::ErrorEnum::ParameterException),
                "当前模型输出格式与 yolov8/yolov9/yolov11/yolov12 "
                "检测模型不匹配，请确认选择的是检测模型而不是分类模型。添加不成功");
        }
    }
}

void ModelServiceImpl::NotifyAlgorithmsChanged(const std::string& modelCode, bool restart_running) {
    auto algorithm_ids = ServiceRegistry::Instance().Get<IAlgorithmQuery>().GetAlgorithmsByModelId(modelCode);
    if (!algorithm_ids.empty()) {
        service::ServiceRegistry::Instance().Get<service::ICameraTaskConfig>().NotifyAlgorithmsChanged(
            algorithm_ids, restart_running);
    }
}

// ──────────────────────────────────────────────
// Config CRUD
// ──────────────────────────────────────────────

cosmo::util::ErrorEnum ModelServiceImpl::GetModelConfig(const std::string& modelCode,
                                                        std::string& configJson) {
    std::string model_dir_path = FindModelDir(modelCode);
    if (model_dir_path.empty()) {
        LOG_WARN("Model directory not found for modelCode: {}", modelCode);
        return cosmo::util::ErrorEnum::FileNotExist;
    }

    std::string config_path = (std::filesystem::path(model_dir_path) / "config.json").string();

    std::ifstream file(config_path);
    if (!file.is_open()) {
        LOG_WARN("Failed to open config.json: {}", config_path);
        return cosmo::util::ErrorEnum::FileNotExist;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    configJson = buffer.str();
    file.close();

    LOG_INFO("Successfully read config.json for modelCode: {}", modelCode);
    return cosmo::util::ErrorEnum::Success;
}

cosmo::util::ErrorEnum ModelServiceImpl::SaveModelConfig(const std::string& modelCode,
                                                         const std::string& configJson) {
    std::string model_dir_path = FindModelDir(modelCode);
    if (model_dir_path.empty()) {
        LOG_WARN("Model directory not found for modelCode: {}", modelCode);
        return cosmo::util::ErrorEnum::FileNotExist;
    }

    std::string config_path = (std::filesystem::path(model_dir_path) / "config.json").string();

    // Validate JSON format
    auto doc = nlohmann::json::parse(configJson, nullptr, false);
    if (doc.is_discarded()) {
        LOG_WARN("{}", "Invalid JSON format in configJson");
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    // Validate model output format (throws cosmo::util::ErrorMessage on failure)
    ValidateModelOutputFormat(doc);

    // Write config.json
    std::ofstream file(config_path);
    if (!file.is_open()) {
        LOG_WARN("Failed to open config.json for writing: {}", config_path);
        return cosmo::util::ErrorEnum::SysErr;
    }

    file << configJson;
    file.close();

    LOG_INFO("Successfully saved config.json for modelCode: {}", modelCode);
    NotifyAlgorithmsChanged(modelCode, true);
    return cosmo::util::ErrorEnum::Success;
}

std::vector<cosmo::Model::MsgModelComponent> ModelServiceImpl::GetModelComponents() {
    const std::string components_file_path = GetModelComponentsJsonPath();
    std::vector<cosmo::Model::MsgModelComponent> result;

    nlohmann::json doc;
    cosmo::util::ErrorEnum ret = ::cosmo::util::JsonFileUtil::ReadJsonArray(components_file_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success) {
        LOG_WARN("Failed to read modelComponents JSON file: {}, using empty list", components_file_path);
        return result;
    }

    for (const auto& component : doc) {
        cosmo::Model::MsgModelComponent msgComponent;

        if (component.contains("id") && component["id"].is_string())
            msgComponent.id = component["id"].get<std::string>();
        if (component.contains("componentName") && component["componentName"].is_string())
            msgComponent.componentName = component["componentName"].get<std::string>();
        if (component.contains("componentType") && component["componentType"].is_string())
            msgComponent.componentType = component["componentType"].get<std::string>();
        if (component.contains("inputParamConfig") && component["inputParamConfig"].is_string())
            msgComponent.inputParamConfig = component["inputParamConfig"].get<std::string>();

        result.push_back(msgComponent);
    }

    LOG_INFO("Successfully read {} model components from {}", result.size(), components_file_path);
    return result;
}

cosmo::util::ErrorEnum ModelServiceImpl::DeleteModel(const std::string& modelCode) {
    if (modelCode.empty()) {
        LOG_WARN("{}", "modelCode is empty in delete request");
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    // Built-in models cannot be deleted (face lib / uniform lib dependency)
    static const std::unordered_set<std::string> kBuiltinModels = {
        "1000001", "1000005", "1000010", "1000012", "1000016",  // Face
        "1001003", "1001007", "1001008"                         // Body
    };
    if (kBuiltinModels.count(modelCode)) {
        LOG_WARN("Cannot delete built-in model: {}", modelCode);
        return cosmo::util::ErrorEnum::DefaultCantBeDelete;
    }

    std::string model_dir_path = FindModelDir(modelCode);
    if (model_dir_path.empty()) {
        LOG_WARN("Model directory not found for modelCode: {}", modelCode);
        return cosmo::util::ErrorEnum::FileNotExist;
    }

    namespace fs = std::filesystem;
    try {
        fs::path modelPath(model_dir_path);
        if (fs::exists(modelPath) && fs::is_directory(modelPath)) {
            std::uintmax_t removedCount = fs::remove_all(modelPath);
            LOG_INFO("Successfully deleted model directory: {}, removed {} entries", model_dir_path,
                     removedCount);
            NotifyAlgorithmsChanged(modelCode, false);
            return cosmo::util::ErrorEnum::Success;
        } else {
            LOG_WARN("Model directory does not exist or is not a directory: {}", model_dir_path);
            return cosmo::util::ErrorEnum::FileNotExist;
        }
    } catch (const fs::filesystem_error& e) {
        LOG_WARN("Filesystem error when deleting model directory {}: {}", model_dir_path, e.what());
        return cosmo::util::ErrorEnum::SysErr;
    } catch (const std::exception& e) {
        LOG_WARN("Exception when deleting model directory {}: {}", model_dir_path, e.what());
        return cosmo::util::ErrorEnum::SysErr;
    }
}

cosmo::util::ErrorEnum ModelServiceImpl::UpdateModel(const std::string& modelCode,
                                                     const std::string& modelName, int max_batch,
                                                     const std::string& description) {
    if (modelCode.empty()) {
        LOG_WARN("{}", "modelCode is empty in update request");
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    // Built-in models cannot be modified (face lib / uniform lib dependency)
    static const std::unordered_set<std::string> kBuiltinModels = {
        "1000001", "1000005", "1000010", "1000012", "1000016",  // Face
        "1001003", "1001007", "1001008"                         // Body
    };
    if (kBuiltinModels.count(modelCode)) {
        LOG_WARN("Cannot update built-in model: {}", modelCode);
        return cosmo::util::ErrorEnum::DefaultCantBeUpdate;
    }

    std::string model_dir_path = FindModelDir(modelCode);
    if (model_dir_path.empty()) {
        LOG_WARN("Model directory not found for modelCode: {}", modelCode);
        return cosmo::util::ErrorEnum::FileNotExist;
    }

    std::string config_path = (std::filesystem::path(model_dir_path) / "config.json").string();

    nlohmann::json doc;
    cosmo::util::ErrorEnum ret = ::cosmo::util::JsonFileUtil::ReadJsonFile(config_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success) {
        LOG_WARN("Failed to read config.json from: {}", config_path);
        return cosmo::util::ErrorEnum::FileNotExist;
    }

    if (doc.contains("models") && doc["models"].is_array() && !doc["models"].empty()) {
        auto& firstModel = doc["models"][0];

        if (!modelName.empty()) {
            firstModel["name"] = modelName;
        }

        if (!description.empty()) {
            firstModel["description"] = description;
        }

        if (max_batch > 0) {
            firstModel["max_batch"] = max_batch;
        }
    } else {
        LOG_WARN("config.json has no valid model array: {}", config_path);
        return cosmo::util::ErrorEnum::InvalidParam;
    }

    std::string json_content = doc.dump();

    std::ofstream file(config_path);
    if (!file.is_open()) {
        LOG_WARN("Failed to open config.json for writing: {}", config_path);
        return cosmo::util::ErrorEnum::SysErr;
    }

    file << json_content;
    file.close();

    LOG_INFO("Successfully updated model info for modelCode: {}, name: {}, max_batch: {}", modelCode,
             modelName, max_batch);
    NotifyAlgorithmsChanged(modelCode, true);
    return cosmo::util::ErrorEnum::Success;
}

// ──────────────────────────────────────────────
// Model path mapping — delegates to ModelPathMapper
// ──────────────────────────────────────────────

void ModelServiceImpl::SetModelPathMapping(const std::string& algCode, const std::string& modelPath) {
    path_mapper_.Set(algCode, modelPath);
}

std::string ModelServiceImpl::GetModelPathMapping(const std::string& algCode) {
    return path_mapper_.Get(algCode);
}

bool ModelServiceImpl::GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath) {
    return path_mapper_.GetModelCfg(algCode, cfgPath, modelPath);
}

bool ModelServiceImpl::GetModelCfg(const std::string& algCode, std::string& cfgPath, std::string& modelPath,
                                   std::string& wordDictPath) {
    return path_mapper_.GetModelCfg(algCode, cfgPath, modelPath, wordDictPath);
}

// ──────────────────────────────────────────────
// Import/Export — delegates to ModelImportExporter
// ──────────────────────────────────────────────

cosmo::util::ErrorEnum ModelServiceImpl::ExportModelConfig(const std::string& modelCode,
                                                           const std::string& modelName,
                                                           std::string& filePath, std::string& fileName) {
    return import_exporter_.ExportModelConfig(modelCode, modelName, filePath, fileName);
}

cosmo::util::ErrorEnum ModelServiceImpl::ImportModel(const std::string& archivePath) {
    return import_exporter_.ImportModel(archivePath);
}

cosmo::util::ErrorEnum ModelServiceImpl::AddAtomicModel(
    const std::string& modelCode, const std::string& modelName, const std::string& model_type,
    const std::string& description, const std::vector<cosmo::Model::BmodelFileInfo>& bmodel_files,
    const std::string& vocabFilePath, const std::string& tokenizerFilePath,
    const std::string& normalizationMode, const std::string& colorChannel) {
    return import_exporter_.AddAtomicModel(modelCode, modelName, model_type, description, bmodel_files,
                                           vocabFilePath, tokenizerFilePath, normalizationMode, colorChannel);
}

// ──────────────────────────────────────────────
// Upload — delegates to ModelUploadHelper
// ──────────────────────────────────────────────

cosmo::util::ErrorEnum ModelServiceImpl::UploadTempFile(
    const std::string& filePath, const std::string& fileName, const std::string& contentLength,
    const std::string& uploadId, const std::string& chunkIndex, const std::string& totalChunks,
    std::string& persistentPath) {
    return upload_helper_.UploadTempFile(filePath, fileName, contentLength, uploadId, chunkIndex, totalChunks,
                                         persistentPath);
}

}  // namespace cosmo::service
