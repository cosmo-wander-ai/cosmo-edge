// ModelConfigParser — implementation.
// Centralizes the config.json parsing logic that was duplicated across
// QueryModels() and QueryAtomicModels().
#include "service/model/impl/ModelConfigParser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "util/ErrorCode.h"
#include "util/JsonFileUtil.h"
#include "util/Log.h"

namespace cosmo::service::detail {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

ParsedModelConfig ModelConfigParser::Parse(const std::string& config_path) {
    ParsedModelConfig result;

    nlohmann::json doc;
    cosmo::util::ErrorEnum ret = cosmo::util::JsonFileUtil::ReadJsonFile(config_path, doc);
    if (ret != cosmo::util::ErrorEnum::Success) {
        LOG_WARN("Failed to read config.json: {}, error: {}", config_path, static_cast<int>(ret));
        return result;
    }

    // Parse algorithm_code (string or int)
    if (doc.contains("algorithm_code") && doc["algorithm_code"].is_string()) {
        result.algorithm_code = doc["algorithm_code"].get<std::string>();
    } else if (doc.contains("algorithm_code") && doc["algorithm_code"].is_number_integer()) {
        result.algorithm_code = std::to_string(doc["algorithm_code"].get<int>());
    } else {
        LOG_WARN("config.json missing algorithm_code: {}", config_path);
        return result;
    }

    // Parse model name from models[0].name
    if (doc.contains("models") && doc["models"].is_array() && !doc["models"].empty()) {
        const auto& first_model = doc["models"][0];
        if (first_model.contains("name") && first_model["name"].is_string()) {
            result.model_name = first_model["name"].get<std::string>();
        } else {
            LOG_WARN("config.json models[0] missing name: {}", config_path);
        }
    } else {
        LOG_WARN("config.json missing or empty models array: {}", config_path);
    }

    // Parse model_type
    if (doc.contains("model_type") && doc["model_type"].is_string()) {
        result.model_type = doc["model_type"].get<std::string>();
    }

    // Parse config.json "version" as fallback
    if (doc.contains("version") && doc["version"].is_string()) {
        result.version = doc["version"].get<std::string>();
    }

    // Parse I/O shapes
    result.input  = ParseIoShape(doc, "inputs", "1");
    result.output = ParseIoShape(doc, "outputs", "5");

    // Parse labels
    result.labels = ParseLabels(doc);

    result.valid = true;
    return result;
}

std::string ModelConfigParser::ParseVersionFromDirName(const std::string& dir_name,
                                                       const std::string& fallback_version) {
    // New format: ..._V1.0.3
    std::regex new_pattern("_V(\\d+)\\.0\\.(\\d+)$");
    std::smatch new_matches;
    if (std::regex_search(dir_name, new_matches, new_pattern) && new_matches.size() > 2) {
        return "V" + new_matches[1].str() + ".0." + new_matches[2].str();
    }

    // Legacy format: ..._v1003
    std::regex old_pattern("_v(\\d+)$");
    std::smatch old_matches;
    if (std::regex_search(dir_name, old_matches, old_pattern) && old_matches.size() > 1) {
        int old_version = std::stoi(old_matches[1].str());
        int major       = (old_version - 1) / 1000 + 1;
        int minor       = (old_version - 1) % 1000;
        return "V" + std::to_string(major) + ".0." + std::to_string(minor);
    }

    return fallback_version.empty() ? "V1.0.0" : fallback_version;
}

std::string ModelConfigParser::JoinShape(const std::vector<int>& shape) {
    std::string result;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0)
            result += ",";
        result += std::to_string(shape[i]);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────

std::vector<ParsedLabel> ModelConfigParser::ParseLabels(const nlohmann::json& doc) {
    std::vector<ParsedLabel> labels;
    if (!doc.contains("labels") || !doc["labels"].is_array())
        return labels;

    for (const auto& entry : doc["labels"]) {
        ParsedLabel label;

        // Parse label id (string or int)
        if (entry.contains("id") && entry["id"].is_string()) {
            label.id = entry["id"].get<std::string>();
        } else if (entry.contains("id") && entry["id"].is_number_integer()) {
            label.id = std::to_string(entry["id"].get<int>());
        }

        // Parse class_name
        if (entry.contains("name") && entry["name"].is_string()) {
            label.class_name = entry["name"].get<std::string>();
        }

        // Parse thresholds
        if (entry.contains("threshold") && entry["threshold"].is_array()) {
            for (const auto& th : entry["threshold"]) {
                if (th.is_number()) {
                    label.thresholds.push_back(th.get<float>());
                }
            }
        }

        // Only include labels with both id and class_name
        if (!label.id.empty() && !label.class_name.empty()) {
            labels.push_back(std::move(label));
        }
    }
    return labels;
}

ParsedIoShape ModelConfigParser::ParseIoShape(const nlohmann::json& doc, const std::string& array_key,
                                              const std::string& default_dim) {
    ParsedIoShape io;
    io.dim = default_dim;

    if (!doc.contains("models") || !doc["models"].is_array() || doc["models"].empty())
        return io;

    const auto& first_model = doc["models"][0];
    if (!first_model.contains(array_key) || !first_model[array_key].is_array())
        return io;

    const auto& io_array = first_model[array_key];
    io.count             = static_cast<int>(io_array.size());

    if (io.count > 0) {
        const auto& first_io = io_array[0];
        if (first_io.contains("shape") && first_io["shape"].is_array()) {
            std::vector<int> shape;
            for (const auto& s : first_io["shape"]) {
                if (s.is_number()) {
                    shape.push_back(s.get<int>());
                }
            }
            if (!shape.empty()) {
                io.dim = JoinShape(shape);
            }
        }
    }

    // Clamp defaults
    if (io.count == 0)
        io.count = 1;
    if (io.dim.empty())
        io.dim = default_dim;

    return io;
}

}  // namespace cosmo::service::detail
