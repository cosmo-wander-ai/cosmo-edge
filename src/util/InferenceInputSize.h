#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json_fwd.hpp"

namespace cosmo {

struct InferenceInputSize {
    int width{0};
    int height{0};

    bool operator==(const InferenceInputSize& other) const;
    bool operator!=(const InferenceInputSize& other) const;
    std::string ToString() const;
};

struct InferenceInputSizes {
    InferenceInputSize default_size;
    std::vector<InferenceInputSize> sizes;
};

// Persisted node values use WxH; model arrays use [H, W].
bool ParseInferenceInputSize(const std::string& value, InferenceInputSize& size);
bool ReadInferenceInputSizes(const nlohmann::json& config, InferenceInputSizes& sizes, std::string& error);
bool ResolveInferenceInputSize(const InferenceInputSizes& sizes,
                               const std::optional<InferenceInputSize>& requested,
                               InferenceInputSize& effective, std::string& error);

void to_json(nlohmann::json& json, const InferenceInputSize& size);
void from_json(const nlohmann::json& json, InferenceInputSize& size);

}  // namespace cosmo
