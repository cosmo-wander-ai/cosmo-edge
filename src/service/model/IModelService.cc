// IModelService — Model service interface — full CRUD, import/export, chunked upload,

#include "IModelService.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void from_json(const nlohmann::json& j, ModelLabel& v) {
    if (j.contains("code") && !j["code"].is_null())
        j.at("code").get_to(v.code);
    if (j.contains("confidenceHigh") && !j["confidenceHigh"].is_null())
        j.at("confidenceHigh").get_to(v.confidenceHigh);
    if (j.contains("confidence") && !j["confidence"].is_null())
        j.at("confidence").get_to(v.confidence);
}

void to_json(nlohmann::json& j, const ModelLabel& v) {
    j["code"]           = v.code;
    j["confidenceHigh"] = v.confidenceHigh;
    j["confidence"]     = v.confidence;
}

void from_json(const nlohmann::json& j, ModelInfo& v) {
    if (j.contains("id") && !j["id"].is_null())
        j.at("id").get_to(v.id);
    if (j.contains("name") && !j["name"].is_null())
        j.at("name").get_to(v.name);
    if (j.contains("version") && !j["version"].is_null())
        j.at("version").get_to(v.version);
    if (j.contains("timestamp") && !j["timestamp"].is_null())
        j.at("timestamp").get_to(v.timestamp);
    if (j.contains("labels") && !j["labels"].is_null())
        j.at("labels").get_to(v.labels);
}

void to_json(nlohmann::json& j, const ModelInfo& v) {
    j["id"]        = v.id;
    j["name"]      = v.name;
    j["version"]   = v.version;
    j["timestamp"] = v.timestamp;
    j["labels"]    = v.labels;
}

}  // namespace cosmo
