#include "util/InferenceInputSize.h"

#include <algorithm>
#include <charconv>
#include <limits>

#include "nlohmann/json.hpp"

namespace cosmo {
namespace {
    bool ReadSize(const nlohmann::json& value, InferenceInputSize& size) {
        if (!value.is_array() || value.size() != 2)
            return false;
        for (const auto& dim : value) {
            if (!dim.is_number_integer() || dim.get<int64_t>() <= 0 ||
                dim.get<int64_t>() > std::numeric_limits<int>::max())
                return false;
        }
        size = {value[1].get<int>(), value[0].get<int>()};
        return true;
    }
}  // namespace

bool InferenceInputSize::operator==(const InferenceInputSize& other) const {
    return width == other.width && height == other.height;
}

bool InferenceInputSize::operator!=(const InferenceInputSize& other) const {
    return !(*this == other);
}

std::string InferenceInputSize::ToString() const {
    return std::to_string(width) + "x" + std::to_string(height);
}

bool ParseInferenceInputSize(const std::string& value, InferenceInputSize& size) {
    const auto separator = value.find('x');
    if (separator == std::string::npos)
        return false;
    InferenceInputSize parsed;
    const auto* begin = value.data();
    const auto* end   = begin + value.size();
    const auto width  = std::from_chars(begin, begin + separator, parsed.width);
    const auto height = std::from_chars(begin + separator + 1, end, parsed.height);
    if (width.ec != std::errc{} || width.ptr != begin + separator || height.ec != std::errc{} ||
        height.ptr != end || parsed.width <= 0 || parsed.height <= 0 || parsed.ToString() != value)
        return false;
    size = parsed;
    return true;
}

bool SupportsInferenceInputSizes(std::string_view model_type) {
    return model_type == "yolo26_det" || model_type == "yolo26_person_attributes";
}

bool ReadInferenceInputSizes(const nlohmann::json& config, InferenceInputSizes& sizes, std::string& error) {
    sizes = {};
    error.clear();
    if (!config.contains("models") || !config["models"].is_array())
        return true;
    bool declared = false;
    for (const auto& model : config["models"]) {
        if (model.is_object() && model.contains("params") && model["params"].is_object() &&
            model["params"].contains("input_sizes"))
            declared = true;
    }
    if (config["models"].empty())
        return true;
    const auto& model = config["models"][0];
    if (model.contains("params") && model["params"].is_object() && model["params"].contains("input_size"))
        ReadSize(model["params"]["input_size"], sizes.default_size);
    if (!declared)
        return true;
    const auto fail = [&error](const char* reason) {
        error = reason;
        return false;
    };
    const auto type = config.find("model_type");
    if (type == config.end() || !type->is_string() ||
        !SupportsInferenceInputSizes(type->get_ref<const std::string&>()) || config["models"].size() != 1 ||
        (model.contains("max_batch") &&
         (!model["max_batch"].is_number_integer() || model["max_batch"] != 1)) ||
        !model.contains("inputs") || !model["inputs"].is_array() || model["inputs"].size() != 1 ||
        !model.contains("outputs") || !model["outputs"].is_array() || model["outputs"].size() != 1)
        return fail("Three input sizes require a single-input, single-output YOLO26 detector with batch 1");
    const auto& params = model["params"];
    const auto& values = params["input_sizes"];
    if (!values.is_array() || values.size() != 3)
        return fail("input_sizes must contain exactly three [height, width] pairs");
    for (const auto& value : values) {
        InferenceInputSize size;
        // The compiled profile defines the external shape. A model can pad it internally.
        if (!ReadSize(value, size) ||
            static_cast<uint64_t>(size.width) * size.height * 3 > std::numeric_limits<int>::max() ||
            std::find(sizes.sizes.begin(), sizes.sizes.end(), size) != sizes.sizes.end())
            return fail("input_sizes must be distinct positive image sizes within tensor capacity");
        sizes.sizes.push_back(size);
    }
    if (std::find(sizes.sizes.begin(), sizes.sizes.end(), sizes.default_size) == sizes.sizes.end())
        return fail("input_size must be one of input_sizes");
    const auto expected = nlohmann::json::array({1, 3, sizes.default_size.height, sizes.default_size.width});
    if (!model["inputs"][0].contains("shape") || model["inputs"][0]["shape"] != expected)
        return fail("The configured input shape must match the default NCHW input size");
    return true;
}

bool ResolveInferenceInputSize(const InferenceInputSizes& sizes,
                               const std::optional<InferenceInputSize>& requested,
                               InferenceInputSize& effective, std::string& error) {
    effective = requested.value_or(sizes.default_size);
    if (requested && (sizes.sizes.empty() ? effective != sizes.default_size
                                          : std::find(sizes.sizes.begin(), sizes.sizes.end(), effective) ==
                                                sizes.sizes.end())) {
        error = "The requested inference input size is not declared by the model";
        return false;
    }
    return true;
}

void to_json(nlohmann::json& json, const InferenceInputSize& size) {
    json = {{"width", size.width}, {"height", size.height}};
}

void from_json(const nlohmann::json& json, InferenceInputSize& size) {
    json.at("width").get_to(size.width);
    json.at("height").get_to(size.height);
}

}  // namespace cosmo
