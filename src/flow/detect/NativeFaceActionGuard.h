#pragma once

#include "flow/detect/NativeFaceParameterGuard.h"
#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo::native_face {
inline bool ConsistentStrictAction(const std::string& required, const std::string& resolved,
                                   const ActionNode& action) {
    std::vector<std::pair<std::string, std::string>> params;
    params.reserve(action.configObject.params.size());
    for (const auto& param : action.configObject.params)
        params.emplace_back(param.key.ToString(), param.value.ToString());
    return ConsistentStrictCode(required, resolved, params);
}
}  // namespace cosmo::native_face
