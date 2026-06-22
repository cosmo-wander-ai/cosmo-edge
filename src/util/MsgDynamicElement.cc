// MsgDynamicElement — Dynamic element types and logic expression types for task configuration.

#include "MsgDynamicElement.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
void to_json(nlohmann::json& j, const MsgDynamicKeyValue& v) {
    j = nlohmann::json{{"key", v.key}};
    if (!static_cast<const std::string&>(v.value).empty())
        j["value"] = v.value;
}

void from_json(const nlohmann::json& j, MsgDynamicKeyValue& v) {
    j.at("key").get_to(v.key);  // mandatory
    if (j.contains("value") && !j["value"].is_null())
        j.at("value").get_to(v.value);
}

void to_json(nlohmann::json& j, const MsgDynamicElement& e) {
    // Base class fields
    to_json(j, static_cast<const MsgDynamicKeyValue&>(e));
    // Always-present fields
    j["name"]         = e.name;
    j["defaultValue"] = e.defaultValue;
    j["description"]  = e.description;
    j["type"]         = e.type;
    j["inputType"]    = e.inputType;
    j["dependsOn"]    = e.dependsOn;
    j["isColumn"]     = e.isColumn;
    j["range"]        = e.range;
    // Conditional fields by type
    if (e.type == "text") {
        j["regexpr"]   = e.regexpr;
        j["failedTip"] = e.failedTip;
        j["queryDict"] = e.queryDict;
    }
    if (e.type == "slider") {
        j["min"] = e.min;
        j["max"] = e.max;
    }
    if (e.type == "radio" || e.type == "check" || e.type == "select") {
        j["options"] = e.options;
    }
    if (e.type == "switch") {
        j["onName"]   = e.onName;
        j["onValue"]  = e.onValue;
        j["offName"]  = e.offName;
        j["offValue"] = e.offValue;
    }
}

void from_json(const nlohmann::json& j, MsgDynamicElement& e) {
    // Base class fields
    from_json(j, static_cast<MsgDynamicKeyValue&>(e));
    // Always-present fields (optional)
    if (j.contains("name") && !j["name"].is_null())
        j.at("name").get_to(e.name);
    if (j.contains("defaultValue") && !j["defaultValue"].is_null())
        j.at("defaultValue").get_to(e.defaultValue);
    if (j.contains("description") && !j["description"].is_null())
        j.at("description").get_to(e.description);
    if (j.contains("type") && !j["type"].is_null())
        j.at("type").get_to(e.type);
    if (j.contains("inputType") && !j["inputType"].is_null())
        j.at("inputType").get_to(e.inputType);
    if (j.contains("dependsOn") && !j["dependsOn"].is_null())
        j.at("dependsOn").get_to(e.dependsOn);
    if (j.contains("isColumn") && !j["isColumn"].is_null())
        j.at("isColumn").get_to(e.isColumn);
    if (j.contains("range") && !j["range"].is_null())
        j.at("range").get_to(e.range);
    // Conditional fields
    if (j.contains("regexpr") && !j["regexpr"].is_null())
        j.at("regexpr").get_to(e.regexpr);
    if (j.contains("failedTip") && !j["failedTip"].is_null())
        j.at("failedTip").get_to(e.failedTip);
    if (j.contains("queryDict") && !j["queryDict"].is_null())
        j.at("queryDict").get_to(e.queryDict);
    if (j.contains("min") && !j["min"].is_null())
        j.at("min").get_to(e.min);
    if (j.contains("max") && !j["max"].is_null())
        j.at("max").get_to(e.max);
    if (j.contains("options") && !j["options"].is_null())
        j.at("options").get_to(e.options);
    if (j.contains("onName") && !j["onName"].is_null())
        j.at("onName").get_to(e.onName);
    if (j.contains("onValue") && !j["onValue"].is_null())
        j.at("onValue").get_to(e.onValue);
    if (j.contains("offName") && !j["offName"].is_null())
        j.at("offName").get_to(e.offName);
    if (j.contains("offValue") && !j["offValue"].is_null())
        j.at("offValue").get_to(e.offValue);
    if (j.contains("available") && !j["available"].is_null())
        j.at("available").get_to(e.available);
    if (j.contains("step") && !j["step"].is_null())
        j.at("step").get_to(e.step);
}

void from_json(const nlohmann::json& j, MsgDynamicElement::Option& v) {
    if (j.contains("name") && !j["name"].is_null())
        j.at("name").get_to(v.name);
    if (j.contains("value") && !j["value"].is_null())
        j.at("value").get_to(v.value);
}

void to_json(nlohmann::json& j, const MsgDynamicElement::Option& v) {
    j["name"]  = v.name;
    j["value"] = v.value;
}

void from_json(const nlohmann::json& j, MsgDynamicElement::DependsOn& v) {
    if (j.contains("key") && !j["key"].is_null())
        j.at("key").get_to(v.key);
    if (j.contains("value") && !j["value"].is_null())
        j.at("value").get_to(v.value);
}

void to_json(nlohmann::json& j, const MsgDynamicElement::DependsOn& v) {
    j["key"]   = v.key;
    j["value"] = v.value;
}

}  // namespace cosmo
