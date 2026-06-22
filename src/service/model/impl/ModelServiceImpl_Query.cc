// clang-format off
#include "service/model/impl/ModelServiceImpl.h"
// clang-format on

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "nlohmann/json.hpp"
#include "service/model/impl/ModelConfigParser.h"
#include "util/DateTimeFormat.h"
#include "util/JsonStructUtil.h"
#include "util/Log.h"
#include "util/PlatformConstants.h"

namespace cosmo::service {

namespace fs = std::filesystem;

void ModelServiceImpl::QueryModels(const std::string& modelName, const std::string& modelCode, int page_num,
                                   int page_size, int& total, std::vector<cosmo::Model::MsgModel>& rows) {
    std::vector<cosmo::Model::MsgModel> all_models;
    std::unordered_set<std::string> seenModelCodes;

    for (const auto& models_dir : GetModelSearchPaths()) {
        std::error_code ec;
        for (const auto& dirEntry : fs::directory_iterator(models_dir, ec)) {
            if (!dirEntry.is_directory())
                continue;

            const auto& entryPath         = dirEntry.path();
            const std::string model_dir   = entryPath.string();
            const std::string config_path = (entryPath / "config.json").string();

            auto cfg = detail::ModelConfigParser::Parse(config_path);
            if (!cfg.valid || cfg.algorithm_code.empty() || seenModelCodes.count(cfg.algorithm_code))
                continue;
            seenModelCodes.insert(cfg.algorithm_code);

            cosmo::Model::MsgModel model;
            model.modelCode = cfg.algorithm_code;
            model.id        = cfg.algorithm_code;
            model.modelName = cfg.model_name;

            // I/O shapes
            model.inputCount  = cfg.input.count;
            model.inputDim    = cfg.input.dim;
            model.outputCount = cfg.output.count;
            model.outputDim   = cfg.output.dim;

            // Version: prefer directory name pattern, fall back to config.json "version"
            model.version = detail::ModelConfigParser::ParseVersionFromDirName(entryPath.filename().string(),
                                                                               cfg.version);

            // Update time from directory modification time
            struct stat st;
            if (::stat(model_dir.c_str(), &st) == 0) {
                auto date        = cosmo::util::GetDateTime(static_cast<int64_t>(st.st_mtime));
                model.updateTime = date.Date().ToYMD() + " " + date.Time().ToHMS();
            }

            model.gpuCode = cosmo::util::kEngineType;

            // Convert parsed labels to MsgModelLabel and serialize to JSON string
            std::vector<cosmo::Model::MsgModelLabel> model_labels;
            for (const auto& pl : cfg.labels) {
                cosmo::Model::MsgModelLabel label;
                label.label      = pl.id;
                label.class_name = pl.class_name;
                label.threshold  = pl.thresholds;
                label.nameCn     = pl.class_name;
                model_labels.push_back(std::move(label));
            }
            if (!model_labels.empty()) {
                (void)cosmo::util::EncodeJson(model_labels, model.label);  // best-effort
            }

            // Apply filters
            if (!modelName.empty() && model.modelName.find(modelName) == std::string::npos)
                continue;
            if (!modelCode.empty() && model.modelCode != modelCode)
                continue;

            model.algorithmNum = 0;
            model.algorithmList.clear();

            all_models.push_back(std::move(model));
        }

        if (ec) {
            LOG_WARN("Failed to iterate models directory: {}", models_dir);
        }
    }

    // Sort by updateTime descending (newest first)
    std::sort(all_models.begin(), all_models.end(),
              [](const cosmo::Model::MsgModel& a, const cosmo::Model::MsgModel& b) {
                  return a.updateTime > b.updateTime;
              });

    total           = static_cast<int>(all_models.size());
    int p_num       = page_num > 0 ? page_num : 1;
    int p_sz        = page_size > 0 ? page_size : 10;
    int start_index = (p_num - 1) * p_sz;
    int end_index   = start_index + p_sz;

    if (start_index < static_cast<int>(all_models.size())) {
        if (end_index > static_cast<int>(all_models.size()))
            end_index = static_cast<int>(all_models.size());
        for (int i = start_index; i < end_index; i++)
            rows.push_back(all_models[i]);
    }
}

std::vector<cosmo::Model::MsgAtomicModel> ModelServiceImpl::QueryAtomicModels(const std::string& modelName,
                                                                              const std::string& modelType,
                                                                              const std::string& filePath) {
    std::vector<cosmo::Model::MsgAtomicModel> result;

    std::vector<std::string> model_dirs;
    std::string requested_path = filePath;
    if (requested_path.size() > 1 && requested_path.back() == '/')
        requested_path.pop_back();
    std::string user_model_path = GetModelPath();
    if (user_model_path.size() > 1 && user_model_path.back() == '/')
        user_model_path.pop_back();
    if (requested_path.empty() || requested_path == user_model_path) {
        model_dirs = GetModelSearchPaths();
    } else {
        model_dirs = {requested_path};
    }

    int scanned_count = 0;
    int valid_count   = 0;
    std::unordered_set<std::string> seenModelCodes;

    for (auto models_dir : model_dirs) {
        // Ensure no trailing slash (except root)
        if (models_dir.size() > 1 && models_dir.back() == '/')
            models_dir.pop_back();

        LOG_INFO("Scanning models directory: {}, modelType filter: '{}', modelName filter: '{}'", models_dir,
                 modelType, modelName);

        std::error_code ec;
        for (const auto& dirEntry : fs::directory_iterator(models_dir, ec)) {
            if (!dirEntry.is_directory())
                continue;

            const std::string model_dir   = dirEntry.path().string();
            const std::string config_path = (dirEntry.path() / "config.json").string();
            scanned_count++;
            LOG_DEBUG("Scanning model directory: {}", model_dir);

            auto cfg = detail::ModelConfigParser::Parse(config_path);
            if (!cfg.valid) {
                LOG_WARN("Skipping model directory (parse failed): {}", model_dir);
                continue;
            }
            if (cfg.algorithm_code.empty() || seenModelCodes.count(cfg.algorithm_code))
                continue;
            seenModelCodes.insert(cfg.algorithm_code);

            LOG_DEBUG("Parsed model: code='{}', name='{}', type='{}'", cfg.algorithm_code, cfg.model_name,
                      cfg.model_type);

            cosmo::Model::MsgAtomicModel model;
            model.atomicCode = cfg.algorithm_code;
            model.atomicName = cfg.model_name;

            // Filter by modelName
            if (!modelName.empty() && model.atomicName.find(modelName) == std::string::npos) {
                LOG_DEBUG("Model name filter mismatch: '{}' (expected: '{}')", model.atomicName, modelName);
                continue;
            }

            // Filter by modelType (category matching)
            if (!modelType.empty()) {
                std::string mt_lower = cfg.model_type, dtLower = modelType;
                std::transform(mt_lower.begin(), mt_lower.end(), mt_lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                std::transform(dtLower.begin(), dtLower.end(), dtLower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                bool type_match = false;
                if (dtLower == "detector")
                    type_match = (mt_lower.find("_det") != std::string::npos || mt_lower == "detector");
                else if (dtLower == "classify")
                    type_match = (mt_lower.find("classify") != std::string::npos || mt_lower == "classify");
                else
                    type_match = (mt_lower == dtLower);
                if (!type_match) {
                    LOG_DEBUG("Model type filter mismatch: '{}' (expected: '{}')", cfg.model_type, modelType);
                    continue;
                }
            }

            // Convert parsed labels to MsgAtomicModelLabel
            for (const auto& pl : cfg.labels) {
                cosmo::Model::MsgAtomicModelLabel label;
                label.label      = pl.id;
                label.class_name = pl.class_name;
                label.threshold  = pl.thresholds.empty() ? 0.0f : pl.thresholds[0];
                label.used       = false;
                label.nameCN     = pl.class_name;
                model.labelList.push_back(std::move(label));
            }

            // Serialize labelList to JSON string for frontend
            if (!model.labelList.empty()) {
                nlohmann::json labelArray = nlohmann::json::array();

                for (const auto& label : model.labelList) {
                    nlohmann::json labelObj = {{"label", label.label},
                                               {"class_name", label.class_name},
                                               {"nameCN", label.nameCN},
                                               {"threshold", {label.threshold}},
                                               {"used", label.used}};
                    labelArray.push_back(std::move(labelObj));
                }

                model.label = labelArray.dump();

                LOG_DEBUG("Generated label JSON for model {}: {} labels", model.atomicCode,
                          model.labelList.size());
            } else {
                LOG_WARN("Model {} has no labels", model.atomicCode);
            }

            if (!model.atomicCode.empty() && !model.atomicName.empty()) {
                result.push_back(std::move(model));
                valid_count++;
                LOG_DEBUG("Added model to list: {} ({})", result.back().atomicCode, result.back().atomicName);
            } else {
                LOG_WARN(
                    "Model missing required fields (code or name empty): code='{}', name='{}', "
                    "path={}",
                    model.atomicCode, model.atomicName, model_dir);
            }
        }
        if (ec)
            LOG_WARN("Error iterating models directory: {}", models_dir);
    }

    LOG_INFO("Model scan complete: scanned={}, valid={}, filtered={}, modelType={}", scanned_count,
             valid_count, result.size(), modelType);
    return result;
}

}  // namespace cosmo::service
