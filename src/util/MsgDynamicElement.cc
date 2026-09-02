// MsgDynamicElement — Dynamic element types and logic expression types for task configuration.

#include "MsgDynamicElement.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <system_error>

#include "util/Keys.h"
#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization
namespace cosmo {
namespace {
    bool DecodeLegacyBoolean(const nlohmann::json& value) {
        if (value.is_boolean()) {
            return value.get<bool>();
        }
        if (value.is_number_unsigned()) {
            const auto number = value.get<uint64_t>();
            if (number <= 1) {
                return number == 1;
            }
        } else if (value.is_number_integer()) {
            const auto number = value.get<int64_t>();
            if (number == 0 || number == 1) {
                return number == 1;
            }
        } else if (value.is_string()) {
            const auto& text = value.get_ref<const std::string&>();
            if (text == "true" || text == "1") {
                return true;
            }
            if (text == "false" || text == "0") {
                return false;
            }
        }
        throw std::invalid_argument("channelEditable must be a boolean or a legacy boolean value");
    }

    int DecodeLegacyInteger(const nlohmann::json& value) {
        if (value.is_number_unsigned()) {
            const auto number = value.get<uint64_t>();
            if (number <= static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                return static_cast<int>(number);
            }
        } else if (value.is_number_integer()) {
            const auto number = value.get<int64_t>();
            if (number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max()) {
                return static_cast<int>(number);
            }
        } else if (value.is_string()) {
            const auto& text        = value.get_ref<const std::string&>();
            int number              = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
            if (error == std::errc{} && end == text.data() + text.size()) {
                return number;
            }
        }
        throw std::invalid_argument("senior must be an integer or a legacy integer string");
    }

    // The current scene editor writes this exact pair when a user unchecks
    // "visible in channel". Historical senior/channelEditable values were
    // derived from older visibility modes and do not prove an intentional hide.
    bool HasExplicitChannelHiddenSelection(const MsgDynamicElement& element) noexcept {
        return element.senior.has_value() && *element.senior == 2 && element.channelEditable.has_value() &&
               !*element.channelEditable;
    }
}  // namespace

bool MsgDynamicElement::IsLegacyChannelEditableException(std::string_view type,
                                                         std::string_view paramKey) noexcept {
    return type == "retroDirect" || paramKey == cosmo::key::CHANNEL_SOURCE_REPEAT;
}

void MsgDynamicElement::NormalizeLegacyChannelOwnership(std::vector<MsgDynamicElement>& elements,
                                                        bool useLegacyVisibilityDefaults) {
    enum class VisitState {
        kUnvisited,
        kVisiting,
        kVisited,
    };

    const auto isLegacySceneOnlyRootControl = [](const MsgDynamicElement& element) {
        const auto key = element.key.ToRefString();
        return (key == "FaceCheck" && element.type == "switch") ||
               (key == "catchView" && element.type == "radio") ||
               ((key == "minFaceWidth" || key == "quality") &&
                (element.type == "text" || element.type == "number"));
    };

    const auto isChannelRenderableAtDepth = [](const MsgDynamicElement& element, size_t depth) {
        const auto key = element.key.ToRefString();
        if (key == cosmo::key::CHANNEL_SOURCE_REPEAT) {
            return depth <= 1;
        }
        if (element.type == "retroDirect") {
            return depth == 0;
        }
        if (depth == 0) {
            return element.type == "select" || element.type == "switch" || element.type == "check" ||
                   element.type == "radio" || element.type == "slider" || element.type == "textarea" ||
                   element.type == "number" || element.type == "text" || element.type == "confidenceConfig" ||
                   element.type == "distanceRate" || element.type == "commoditySet" ||
                   element.type == "workClothesSet" || element.type == "faceSet";
        }
        if (depth == 1) {
            return element.type == "select" || element.type == "switch" || element.type == "check" ||
                   element.type == "radio" || element.type == "slider" || element.type == "textarea" ||
                   element.type == "text" || element.type == "confidenceConfig" ||
                   element.type == "distanceRate" || element.type == "commoditySet" ||
                   element.type == "workClothesSet" || element.type == "faceSet";
        }
        return false;
    };

    // The channel editor renders only roots and one dependency level. Resolve the real dependency
    // depth first. Malformed links, missing parents and every member/descendant of a cycle remain
    // unresolved and therefore fail closed; an explicit channelEditable=true cannot promote them.
    std::vector<VisitState> depthStates(elements.size(), VisitState::kUnvisited);
    std::vector<std::optional<size_t>> dependencyDepths(elements.size());
    const auto resolveDepth = [&](auto&& self, size_t index) -> std::optional<size_t> {
        if (depthStates[index] == VisitState::kVisited) {
            return dependencyDepths[index];
        }
        if (depthStates[index] == VisitState::kVisiting) {
            return std::nullopt;
        }

        depthStates[index] = VisitState::kVisiting;
        std::optional<size_t> depth;
        const auto& element = elements[index];
        if (!element.malformedDependsOn) {
            if (element.dependsOn.key.empty()) {
                depth = 0;
            } else {
                const auto parent =
                    std::find_if(elements.begin(), elements.end(), [&](const auto& candidate) {
                        return candidate.key.ToRefString() == element.dependsOn.key;
                    });
                if (parent != elements.end()) {
                    const auto parentDepth =
                        self(self, static_cast<size_t>(std::distance(elements.begin(), parent)));
                    if (parentDepth.has_value()) {
                        depth = *parentDepth + 1;
                    }
                }
            }
        }

        dependencyDepths[index] = depth;
        depthStates[index]      = VisitState::kVisited;
        return depth;
    };
    for (size_t index = 0; index < elements.size(); ++index) {
        (void)resolveDepth(resolveDepth, index);
    }

    // DynamicForm renders one paired point control and keeps only the last
    // root initial/end descriptor. Orphans and earlier duplicates must not
    // become invisible channel overrides.
    std::optional<size_t> initialPointIndex;
    std::optional<size_t> endPointIndex;
    for (size_t index = 0; index < elements.size(); ++index) {
        if (dependencyDepths[index] != 0) {
            continue;
        }
        if (elements[index].type == "initialPoint") {
            initialPointIndex = index;
        } else if (elements[index].type == "endPoint") {
            endPointIndex = index;
        }
    }

    std::vector<bool> channelRenderable(elements.size(), false);
    for (size_t index = 0; index < elements.size(); ++index) {
        if (!dependencyDepths[index].has_value()) {
            continue;
        }
        const auto depth         = *dependencyDepths[index];
        const bool isPairedPoint = initialPointIndex.has_value() && endPointIndex.has_value() &&
                                   (index == *initialPointIndex || index == *endPointIndex);
        channelRenderable[index] = isPairedPoint || isChannelRenderableAtDepth(elements[index], depth);
        if (!channelRenderable[index] || depth != 1) {
            continue;
        }

        const auto& element = elements[index];
        const auto parent   = std::find_if(elements.begin(), elements.end(), [&](const auto& candidate) {
            return candidate.key.ToRefString() == element.dependsOn.key;
        });
        channelRenderable[index] =
            parent != elements.end() && (parent->type == "switch" || parent->type == "select");
    }

    std::vector<VisitState> states(elements.size(), VisitState::kUnvisited);
    std::vector<bool> editable(elements.size(), false);
    const auto resolve = [&](auto&& self, size_t index) -> bool {
        const auto& element = elements[index];
        if (states[index] == VisitState::kVisited) {
            return editable[index];
        }
        if (states[index] == VisitState::kVisiting) {
            return false;
        }

        if (!dependencyDepths[index].has_value() || !channelRenderable[index]) {
            states[index]   = VisitState::kVisited;
            editable[index] = false;
            return false;
        }

        states[index]    = VisitState::kVisiting;
        const auto depth = *dependencyDepths[index];
        bool result;
        if (useLegacyVisibilityDefaults) {
            // This mode is used only to classify provenance-less snapshots
            // created by an older release. Preserve what that release could
            // actually expose instead of applying today's visible default.
            if (element.channelEditable.has_value()) {
                result = *element.channelEditable;
            } else if (depth == 0 && isLegacySceneOnlyRootControl(element)) {
                result = false;
            } else if (IsLegacyChannelEditableException(element.type, element.key.ToRefString())) {
                result = true;
            } else if (element.senior.has_value() && (*element.senior == 1 || *element.senior == 2)) {
                result = false;
            } else {
                result = true;
            }
        } else {
            // Every renderable parameter is channel-editable by default. Only
            // the canonical pair produced by an explicit unchecked selection
            // remains scene-owned; legacy markers are promoted.
            result = !HasExplicitChannelHiddenSelection(element);
        }

        // A child is reachable only through its complete editable parent chain. This check comes
        // after the child's local ownership rule but remains mandatory for explicit and legacy-forced
        // controls alike.
        if (result && depth > 0) {
            const auto parent = std::find_if(elements.begin(), elements.end(), [&](const auto& candidate) {
                return candidate.key.ToRefString() == element.dependsOn.key;
            });
            result            = parent != elements.end() &&
                     self(self, static_cast<size_t>(std::distance(elements.begin(), parent)));
        }

        editable[index] = result;
        states[index]   = VisitState::kVisited;
        return result;
    };

    for (size_t index = 0; index < elements.size(); ++index) {
        (void)resolve(resolve, index);
    }

    bool pairedPointManaged = false;
    if (initialPointIndex.has_value() && endPointIndex.has_value() &&
        (!editable[*initialPointIndex] || !editable[*endPointIndex])) {
        editable[*initialPointIndex] = false;
        editable[*endPointIndex]     = false;
        pairedPointManaged           = true;
    }
    for (size_t index = 0; index < elements.size(); ++index) {
        if (pairedPointManaged && (index == *initialPointIndex || index == *endPointIndex)) {
            editable[index] = false;
        }
        elements[index].channelEditable = editable[index];
    }
}

bool MsgDynamicElement::IsChannelEditable() const noexcept {
    if (malformedDependsOn) {
        return false;
    }
    if (channelEditable.has_value()) {
        return *channelEditable;
    }
    return true;
}

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
    if (e.channelEditable.has_value())
        j["channelEditable"] = *e.channelEditable;
    if (e.legacyChannelEditable.has_value())
        j["legacyChannelEditable"] = *e.legacyChannelEditable;
    if (e.senior.has_value())
        j["senior"] = *e.senior;
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
    e.dependsOn          = {};
    e.malformedDependsOn = false;
    if (j.contains("dependsOn") && !j["dependsOn"].is_null()) {
        const auto& dependency = j.at("dependsOn");
        if (!dependency.is_object() || !dependency.contains("key") || dependency["key"].is_null() ||
            !dependency["key"].is_string()) {
            e.malformedDependsOn = true;
        } else {
            dependency.get_to(e.dependsOn);
        }
    }
    if (j.contains("isColumn") && !j["isColumn"].is_null())
        j.at("isColumn").get_to(e.isColumn);
    e.channelEditable.reset();
    if (j.contains("channelEditable") && !j["channelEditable"].is_null())
        e.channelEditable = DecodeLegacyBoolean(j.at("channelEditable"));
    e.legacyChannelEditable.reset();
    if (j.contains("legacyChannelEditable") && !j["legacyChannelEditable"].is_null())
        e.legacyChannelEditable = DecodeLegacyBoolean(j.at("legacyChannelEditable"));
    e.senior.reset();
    if (j.contains("senior") && !j["senior"].is_null())
        e.senior = DecodeLegacyInteger(j.at("senior"));
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
