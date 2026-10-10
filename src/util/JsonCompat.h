// JSON compatibility layer — unified type alias.
// All project code should include this header (or nlohmann/json_fwd.hpp) instead
// of including nlohmann/json.hpp directly in header files.

#pragma once

#include "nlohmann/json.hpp"

namespace cosmo::util {

/// Canonical JSON type alias for the entire project.
using Json = nlohmann::json;

}  // namespace cosmo::util
