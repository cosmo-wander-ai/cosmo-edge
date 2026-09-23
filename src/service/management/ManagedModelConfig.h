#pragma once
#include <set>

#include "service/management/ManagementService.h"

namespace cosmo::service {
// Device-derived tensor metadata and file identities are never taken from the platform.
inline nlohmann::json MergeManagedModelConfig(const nlohmann::json& native, const nlohmann::json& requested) {
    using Json   = nlohmann::json;
    auto require = [](bool valid) {
        if (!valid)
            throw ManagementError("INVALID_MODEL_CONFIGURATION");
    };
    require(native.is_object() && requested.is_object());
    require(requested.value("model_type", Json{}) == native.value("model_type", Json{}));
    require(requested.value("chip_type", Json{}) == native.value("chip_type", Json{}));
    require(requested.contains("models") && requested["models"].is_array() && native.contains("models") &&
            native["models"].is_array() && requested["models"].size() == native["models"].size());
    auto result = native;
    for (size_t i = 0; i < native["models"].size(); ++i) {
        const auto& source = requested["models"][i];
        const auto& actual = native["models"][i];
        require(source.is_object());
        for (const auto* field : {"inputs", "outputs"}) {
            require(source.value(field, Json::array()) == actual.value(field, Json::array()));
        }
        if (source.contains("params")) {
            require(source["params"].is_object());
            result["models"][i]["params"] = source["params"];
        }
    }
    if (requested.contains("labels")) {
        require(requested["labels"].is_array() && requested["labels"].size() <= 80);
        std::set<std::string> ids;
        for (const auto& label : requested["labels"]) {
            require(label.is_object() && label.contains("id") && label["id"].is_string() &&
                    label.contains("name") && label["name"].is_string() && label.contains("threshold") &&
                    label["threshold"].is_array() && label["threshold"].size() == 2);
            require(ids.insert(label["id"].get<std::string>()).second);
            for (const auto& value : label["threshold"])
                require(value.is_number() && value >= 0 && value <= 1);
        }
        result["labels"] = requested["labels"];
    }
    if (requested.contains("config") && requested["config"].contains("generation")) {
        require(requested["config"]["generation"].is_object());
        const auto type = native.value("model_type", std::string{});
        require(type == "qwen3vl" || type == "qwen3_5");
        result["config"]["generation"] = requested["config"]["generation"];
    }
    return result;
}
}  // namespace cosmo::service
