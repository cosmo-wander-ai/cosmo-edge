#pragma once

#include <algorithm>
#include <string>

#include "flow/detect/NativeModelSelector.h"
#include "util/EnvUtil.h"
#include "util/PathUtil.h"

namespace cosmo::native_face {
// Shared with the execution selector. The process configuration is captured
// once; a request cannot supply or override it.
inline const RequiredCodes& ProcessRequiredCodes() {
    static const auto value =
        ParseRequiredCodes(util::GetEnvOrDefault("COSMO_AXERA_NATIVE_FACE_REQUIRED_CODE", ""),
                           util::GetEnvOrDefault("COSMO_AXERA_NATIVE_REQUIRED_CODES", ""),
                           [](const std::string& code) { return path::IsSafePathComponent(code, 120); });
    return value;
}
inline bool ProcessRequires(const std::string& code) {
    const auto& required = ProcessRequiredCodes();
    return required.valid &&
           std::find(required.codes.begin(), required.codes.end(), code) != required.codes.end();
}
}  // namespace cosmo::native_face
