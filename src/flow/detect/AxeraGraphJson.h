#pragma once

#include <nlohmann/json.hpp>

namespace cosmo::axera_graph_admission {
inline bool ContainsControlledModel(const nlohmann::json& value) {
    if (value.is_string()) {
        const auto& s = value.get_ref<const std::string&>();
        return s == "1001003" || s == "9275710";
    }
    if (value.is_number_integer())
        return value == 1001003 || value == 9275710;
    if (value.is_array() || value.is_object()) {
        for (const auto& item : value)
            if (ContainsControlledModel(item))
                return true;
    }
    return false;
}
}  // namespace cosmo::axera_graph_admission
