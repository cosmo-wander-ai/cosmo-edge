// ModelConfigParser — shared config.json parsing logic for model queries.
// Extracted from ModelServiceImpl_Query.cc to eliminate duplicated JSON parsing
// between QueryModels() and QueryAtomicModels().
#pragma once

#include <string>
#include <vector>

#include "nlohmann/json_fwd.hpp"
#include "util/ErrorCode.h"

namespace cosmo::service::detail {

// Parsed label from config.json "labels" array.
struct ParsedLabel {
    std::string id;
    std::string class_name;
    std::vector<float> thresholds;
};

// Parsed I/O shape info from config.json "models[0].inputs/outputs".
struct ParsedIoShape {
    int count{1};
    std::string dim{"1"};
};

// Common fields extracted from a model's config.json.
// Both QueryModels and QueryAtomicModels consume this intermediate representation.
struct ParsedModelConfig {
    std::string algorithm_code;
    std::string model_name;
    std::string model_type;
    std::string version;  // From config.json "version" field (fallback only)
    ParsedIoShape input;
    ParsedIoShape output;
    std::vector<ParsedLabel> labels;
    bool valid{false};  // True if algorithm_code was successfully parsed
};

// Static utility that reads and parses a model's config.json into ParsedModelConfig.
class ModelConfigParser {
public:
    ModelConfigParser() = delete;

    // Read config.json from `config_path` and parse common fields.
    // Returns a ParsedModelConfig with `valid == false` if the file cannot be read
    // or algorithm_code is missing.
    static ParsedModelConfig Parse(const std::string& config_path);

    // Parse version string from the model directory name.
    // Supports new format "..._V1.0.3" and legacy "_v1003".
    // Falls back to `fallback_version` if no pattern matches.
    static std::string ParseVersionFromDirName(const std::string& dir_name,
                                               const std::string& fallback_version);

    // Join a shape vector (e.g., {1, 3, 640, 640}) into a comma-separated string.
    static std::string JoinShape(const std::vector<int>& shape);

private:
    // Parse the "labels" array from a nlohmann::json Document.
    static std::vector<ParsedLabel> ParseLabels(const nlohmann::json& doc);

    // Parse I/O shape from "models[0].inputs" or "models[0].outputs".
    static ParsedIoShape ParseIoShape(const nlohmann::json& doc, const std::string& array_key,
                                      const std::string& default_dim);
};

}  // namespace cosmo::service::detail
