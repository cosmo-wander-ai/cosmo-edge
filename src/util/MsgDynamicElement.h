// Dynamic element types and logic expression types for task configuration.

#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "util/LimitedType.h"

namespace cosmo {

// Dynamic key-value pair with expandable keys/values.
struct MsgDynamicKeyValue {
    util::String<1, 512> key;
    util::String<0, 2056> value;
    std::vector<std::string> keys;    // Expanded keys
    std::vector<std::string> values;  // Expanded values
};

void to_json(nlohmann::json& j, const MsgDynamicKeyValue& v);
void from_json(const nlohmann::json& j, MsgDynamicKeyValue& v);

// Dynamic UI element descriptor for task configuration forms.
struct MsgDynamicElement : public MsgDynamicKeyValue {
    struct Option {
        std::string name;
        std::string value;
        friend void to_json(nlohmann::json& j, const Option& v);
        friend void from_json(const nlohmann::json& j, Option& v);
    };
    struct DependsOn {
        std::string key;
        std::string value;
        friend void to_json(nlohmann::json& j, const DependsOn& v);
        friend void from_json(const nlohmann::json& j, DependsOn& v);
    };
    std::string name;
    std::string defaultValue;
    std::string description;
    std::string type;
    std::string inputType;
    std::string regexpr;
    std::string failedTip;
    std::string queryDict;
    float step{0.0f};
    float min{0.0f};
    float max{0.0f};
    bool isColumn{false};
    std::string range;
    std::vector<Option> options;
    DependsOn dependsOn;
    std::string onName;
    int onValue{1};
    std::string offName;
    int offValue{0};
    bool available{true};
};

// Conditional serialization: serialize different fields based on 'type'.
void to_json(nlohmann::json& j, const MsgDynamicElement& e);

void from_json(const nlohmann::json& j, MsgDynamicElement& e);

}  // namespace cosmo
