// LinkageUtil.h — Shared internal utilities for LinkageServiceImpl.
// NOT part of the public API. Only included by LinkageServiceImpl.cc and LinkageCrud.cc.

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace cosmo::service::detail {

/// Normalize a workflow JSON string: unwrap double-encoded strings,
/// ensure consistent serialized form.
inline std::string NormalizeWorkflowJson(const std::string& raw) {
    if (raw.empty()) {
        return "[]";
    }
    auto doc = nlohmann::json::parse(raw, nullptr, false);
    if (doc.is_discarded()) {
        return raw;
    }

    if (doc.is_array() || doc.is_object()) {
        return doc.dump();
    }

    if (doc.is_string()) {
        std::string inner = doc.get<std::string>();
        auto inner_doc    = nlohmann::json::parse(inner, nullptr, false);
        if (!inner_doc.is_discarded() && (inner_doc.is_array() || inner_doc.is_object())) {
            return inner_doc.dump();
        }
        return inner;
    }

    return raw;
}

}  // namespace cosmo::service::detail
