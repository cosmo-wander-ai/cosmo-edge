#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cosmo {

// Immutable schema snapshots travel with records. Labels may change without
// reinterpreting historical values. Source nodes disambiguate reused models.
struct AttributeOption {
    std::string label;
    std::string value;
    std::string name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeOption, label, value, name)

struct AttributeDefinition {
    std::string key;
    std::string name;
    std::string sourceNode;
    std::string modelCode;
    std::string type{"single"};  // single, multiple, or binary
    double threshold{0.5};
    double minRatio{0.6};
    // Binary: [positive model label, negative outcome with an empty label].
    std::vector<AttributeOption> options;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeDefinition, key, name, sourceNode, modelCode, type,
                                                threshold, minRatio, options)

struct AttributeSchema {
    std::string objectType{"person"};
    std::vector<AttributeDefinition> attributes;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeSchema, objectType, attributes)

struct AttributeValue {
    std::string key;
    std::string status{"unknown"};
    std::vector<std::string> values;
    double confidence{0};
    int64_t samples{0};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeValue, key, status, values, confidence, samples)

struct AttributeRecord {
    std::string recordId;
    std::string trackId;
    std::string schemaId;
    AttributeSchema schema;
    int64_t firstSeen{0};
    int64_t lastSeen{0};
    std::string status{"partial"};
    std::vector<std::string> areaIds;
    std::vector<AttributeValue> attributes;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeRecord, recordId, trackId, schemaId, schema,
                                                firstSeen, lastSeen, status, areaIds, attributes)

struct AttributeFilter {
    std::string key;
    std::string value;
    std::string status;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeFilter, key, value, status)

struct AttributeStatistics {
    std::string schemaId;
    std::string key;
    std::string value;
    std::string status;
    int64_t count{0};
    int64_t validCount{0};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeStatistics, schemaId, key, value, status, count,
                                                validCount)

struct AttributeSummary {
    std::vector<AttributeStatistics> statistics;
    nlohmann::json schemas = nlohmann::json::array();
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AttributeSummary, statistics, schemas)

// Reject ambiguous mappings and bound both runtime state and SQL/UI expansion.
bool ValidateAttributeSchema(const AttributeSchema& schema);
bool ValidateAttributeRecord(const AttributeRecord& record);
bool ValidateAttributeFlow(const std::string& process, bool requireAttributes = false);
std::string AttributeSchemaId(const AttributeSchema& schema);
std::string AttributeFingerprint(const std::string& value);

}  // namespace cosmo
