#include "util/AttributeAnalysis.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace cosmo {
namespace {
    bool Identifier(const std::string& value) {
        if (value.empty() || value.size() > 64)
            return false;
        for (unsigned char c : value) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '-' || c == '.'))
                return false;
        }
        return true;
    }
}  // namespace

bool ValidateAttributeSchema(const AttributeSchema& schema) {
    if (!Identifier(schema.objectType) || schema.attributes.empty() || schema.attributes.size() > 32)
        return false;
    std::set<std::string> keys;
    for (const auto& attr : schema.attributes) {
        if (!Identifier(attr.key) || !keys.insert(attr.key).second || attr.name.empty() ||
            attr.name.size() > 128 || !Identifier(attr.sourceNode) || !Identifier(attr.modelCode) ||
            (attr.type != "single" && attr.type != "multiple" && attr.type != "binary") ||
            !std::isfinite(attr.threshold) || attr.threshold < 0 || attr.threshold > 1 ||
            !std::isfinite(attr.minRatio) || attr.minRatio <= 0.5 || attr.minRatio > 1 ||
            attr.options.empty() || attr.options.size() > 64)
            return false;
        const bool binary = attr.type == "binary";
        if (binary && (attr.options.size() != 2 || attr.options.front().label.empty() ||
                       !attr.options.back().label.empty()))
            return false;
        std::set<std::string> labels, values;
        for (const auto& option : attr.options) {
            if ((!binary && option.label.empty()) || option.label.size() > 128 || !Identifier(option.value) ||
                option.name.empty() || option.name.size() > 128 || !labels.insert(option.label).second ||
                !values.insert(option.value).second)
                return false;
        }
    }
    return nlohmann::json(schema).dump().size() <= 65536;
}

std::string AttributeSchemaId(const AttributeSchema& schema) {
    return AttributeFingerprint(nlohmann::json(schema).dump());
}

bool ValidateAttributeRecord(const AttributeRecord& record) {
    if (!ValidateAttributeSchema(record.schema) || record.recordId.empty() || record.trackId.empty() ||
        record.schemaId != AttributeSchemaId(record.schema) || record.firstSeen < 0 ||
        record.lastSeen < record.firstSeen || record.attributes.size() != record.schema.attributes.size())
        return false;
    std::set<std::string> keys;
    bool complete = true;
    for (const auto& value : record.attributes) {
        const auto definition = std::find_if(record.schema.attributes.begin(), record.schema.attributes.end(),
                                             [&](const auto& item) { return item.key == value.key; });
        if (definition == record.schema.attributes.end() || !keys.insert(value.key).second ||
            value.samples < 0 || !std::isfinite(value.confidence) || value.confidence < 0 ||
            value.confidence > 1)
            return false;
        if (value.status != "valid") {
            complete = false;
            if ((value.status != "unknown" && value.status != "failed") || !value.values.empty())
                return false;
            continue;
        }
        if (value.samples == 0 || value.values.empty() ||
            (definition->type != "multiple" && value.values.size() != 1))
            return false;
        std::set<std::string> values;
        for (const auto& option : value.values) {
            if (!values.insert(option).second ||
                std::none_of(definition->options.begin(), definition->options.end(),
                             [&](const auto& item) { return item.value == option; }))
                return false;
        }
    }
    return record.status == (complete ? "complete" : "partial");
}

bool ValidateAttributeFlow(const std::string& process, bool requireAttributes) {
    if (process.empty())
        return !requireAttributes;
    try {
        const auto nodes = nlohmann::json::parse(process);
        if (!nodes.is_array())
            return false;
        auto param = [](const nlohmann::json& node, const std::string& key) -> std::string {
            if (!node.contains("configObject"))
                return {};
            for (const auto& value : node.at("configObject").value("params", nlohmann::json::array())) {
                if (value.value("key", "") == key)
                    return value.value("value", "");
            }
            return {};
        };
        std::map<std::string, const nlohmann::json*> byId;
        const nlohmann::json* accumulation = nullptr;
        for (const auto& node : nodes) {
            if (node.value("actionId", "") == "BA_20003" && param(node, "inputMode") == "attributes") {
                if (accumulation)
                    return false;
                accumulation = &node;
            }
        }
        if (!accumulation)
            return !requireAttributes;
        for (const auto& node : nodes) {
            if (!byId.emplace(node.at("flowActionId").get<std::string>(), &node).second)
                return false;
        }
        const auto schema =
            nlohmann::json::parse(param(*accumulation, "attributeSchema")).get<AttributeSchema>();
        if (!ValidateAttributeSchema(schema))
            return false;
        auto ancestors = [&](const nlohmann::json& node) {
            std::set<std::string> seen;
            std::function<void(const nlohmann::json&)> visit = [&](const auto& current) {
                std::istringstream parents(current.value("preFlowActionId", ""));
                std::string id;
                while (std::getline(parents, id, ',')) {
                    if (byId.count(id) && seen.insert(id).second)
                        visit(*byId.at(id));
                }
            };
            visit(node);
            return seen;
        };
        const auto upstream = ancestors(*accumulation);
        // The first release accepts one sequential observation path. Fan-in alone
        // does not merge per-target classifier evidence from independent branches.
        for (const auto& id : upstream) {
            if (byId.at(id)->value("preFlowActionId", "").find(',') != std::string::npos)
                return false;
        }
        if (accumulation->value("preFlowActionId", "").find(',') != std::string::npos)
            return false;
        for (const auto& definition : schema.attributes) {
            if (!upstream.count(definition.sourceNode))
                return false;
            const auto& source   = *byId.at(definition.sourceNode);
            const auto preceding = ancestors(source);
            if (std::none_of(preceding.begin(), preceding.end(), [&](const auto& id) {
                    return byId.at(id)->value("actionId", "") == "AA_00003";
                }))
                return false;
            if (source.value("actionId", "") != "AA_00002" ||
                param(source, "atomicCode") != definition.modelCode)
                return false;
        }
        for (const auto& node : nodes) {
            if (node.value("actionId", "") == "BA_00004" &&
                ancestors(node).count(accumulation->at("flowActionId").get<std::string>()))
                return true;
        }
        return false;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

std::string AttributeFingerprint(const std::string& value) {
    // Stable content fingerprint (not a security signature).
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}
}  // namespace cosmo
