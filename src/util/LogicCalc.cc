// LogicCalc — Dynamic logic expression types for task configuration.

#include "LogicCalc.h"

#include <nlohmann/json.hpp>

#include "util/LimitedTypeJson.h"

// Auto-generated JSON serialization

// Enum serialization (moved from LogicCalc.h to avoid json.hpp in headers)
NLOHMANN_JSON_SERIALIZE_ENUM(cosmo::LogicType, {
                                                   {cosmo::LogicType::INVALID, 0},
                                                   {cosmo::LogicType::OR, 1},
                                                   {cosmo::LogicType::AND, 2},
                                                   {cosmo::LogicType::NOR, 3},
                                                   {cosmo::LogicType::Equal, 11},
                                                   {cosmo::LogicType::NEQ, 12},
                                                   {cosmo::LogicType::Greater, 13},
                                                   {cosmo::LogicType::GE, 14},
                                                   {cosmo::LogicType::Less, 15},
                                                   {cosmo::LogicType::LE, 16},
                                                   {cosmo::LogicType::EMPTY, 21},
                                                   {cosmo::LogicType::NonEmpty, 22},
                                                   {cosmo::LogicType::Include, 31},
                                                   {cosmo::LogicType::NonInclude, 32},
                                               })

namespace cosmo {
void to_json(nlohmann::json& j, const LogicCalc& c) {
    j["type"] = c.type;
    if (!c.list.empty())
        j["list"] = c.list;
    if (!static_cast<const std::string&>(c.keyL).empty())
        j["keyL"] = c.keyL;
    if (!static_cast<const std::string&>(c.keyR).empty())
        j["keyR"] = c.keyR;
}

void from_json(const nlohmann::json& j, LogicCalc& c) {
    j.at("type").get_to(c.type);  // mandatory
    if (j.contains("list") && !j["list"].is_null())
        j.at("list").get_to(c.list);
    if (j.contains("keyL") && !j["keyL"].is_null())
        j.at("keyL").get_to(c.keyL);
    if (j.contains("keyR") && !j["keyR"].is_null())
        j.at("keyR").get_to(c.keyR);
}

}  // namespace cosmo
