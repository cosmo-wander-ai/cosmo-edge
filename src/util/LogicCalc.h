// Dynamic logic expression types for task configuration.
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "util/LimitedType.h"

namespace cosmo {

enum class LogicType {
    INVALID = 0,
    // Logical operators
    OR  = 1,
    AND = 2,
    NOR = 3,  // Not (reserved)

    // Arithmetic operators
    Equal   = 11,
    NEQ     = 12,
    Greater = 13,
    GE      = 14,
    Less    = 15,
    LE      = 16,

    EMPTY    = 21,  // Is empty (right operand is null)
    NonEmpty = 22,  // Is not empty (right operand is null)

    // Checkbox constraints
    Include    = 31,
    NonInclude = 32,
};

constexpr bool IsValidLogicType(LogicType type) {
    return ((type >= LogicType::OR) && (type <= LogicType::NOR)) ||
           ((type >= LogicType::Equal) && (type <= LogicType::LE)) ||
           ((type >= LogicType::EMPTY) && (type <= LogicType::NonEmpty)) ||
           ((type >= LogicType::Include) && (type <= LogicType::NonInclude));
}

// LogicType JSON serialization is in LogicCalc.cc (NLOHMANN_JSON_SERIALIZE_ENUM)

struct LogicCalc {
    LogicType type{LogicType::INVALID};
    util::String<0, 256> keyL;
    std::vector<std::string> keyLElements;
    util::String<0, 256> keyR;
    std::vector<std::string> keyRElements;
    std::vector<LogicCalc> list;
};

// Hand-written because 'type' is mandatory (M), rest are optional (O)
void to_json(nlohmann::json& j, const LogicCalc& c);

void from_json(const nlohmann::json& j, LogicCalc& c);

// Convert LogicType to human-readable string for logging
inline std::string_view LogicString(LogicType type) {
    switch (type) {
        case LogicType::OR:
            return "OR";
        case LogicType::AND:
            return "AND";
        case LogicType::NOR:
            return "NOR";
        case LogicType::Equal:
            return "Equal";
        case LogicType::NEQ:
            return "NEQ";
        case LogicType::Greater:
            return "Greater";
        case LogicType::GE:
            return "GE";
        case LogicType::Less:
            return "Less";
        case LogicType::LE:
            return "LE";
        case LogicType::EMPTY:
            return "EMPTY";
        case LogicType::NonEmpty:
            return "NonEmpty";
        case LogicType::Include:
            return "Include";
        case LogicType::NonInclude:
            return "NonInclude";
        default:
            return "LogicTypeUnknown";
    }
}

}  // namespace cosmo
