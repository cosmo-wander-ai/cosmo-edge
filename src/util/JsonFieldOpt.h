// Optional JSON field extraction macros — eliminates repetitive
// `if (j.contains("key") && !j["key"].is_null()) j.at("key").get_to(v.key);`
// boilerplate in from_json() implementations.
//
// Usage:
//   JSON_OPT(j, v, fieldName);         — key == member name
//   JSON_OPT_KEY(j, v, "key", field);  — key differs from member name

#pragma once

#include <nlohmann/json_fwd.hpp>

namespace cosmo::json_detail {

// Read a JSON field into `out` if the field exists and is not null.
template <typename T>
inline void ReadOpt(const nlohmann::json& j, const char* key, T& out) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) {
        it->get_to(out);
    }
}

}  // namespace cosmo::json_detail

// Read an optional field: JSON key == member name
#define JSON_OPT(j, v, field) ::cosmo::json_detail::ReadOpt(j, #field, (v).field)

// Read an optional field with a different JSON key name
#define JSON_OPT_KEY(j, v, key, field) ::cosmo::json_detail::ReadOpt(j, key, (v).field)
