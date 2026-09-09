// Model metadata shared by query consumers.
#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace cosmo {
struct ModelLabel {
    std::string code;
    std::string labelName;
    std::string label;
    float confidenceHigh{-1.0};
    float confidence{-1.0};
    friend void to_json(nlohmann::json& j, const ModelLabel& v);
    friend void from_json(const nlohmann::json& j, ModelLabel& v);
};

struct ModelInfo {
    std::string id;
    std::string name;
    std::string version;
    int64_t timestamp{0};
    std::string path;
    std::vector<ModelLabel> labels;
    friend void to_json(nlohmann::json& j, const ModelInfo& v);
    friend void from_json(const nlohmann::json& j, ModelInfo& v);
};
}  // namespace cosmo
