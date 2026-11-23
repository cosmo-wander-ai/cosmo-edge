#pragma once

#include "util/InferenceInputSize.h"
#include "util/Keys.h"
#include "util/SafeParse.h"
#include "util/StringUtil.h"
#include "util/dto/AlgorithmMsgTypes.h"

namespace cosmo::service::action_model_params {

// Use the same model aliases and ordering as runtime action loading. This is
// intentionally not an authorization or a model identity check.
inline bool Extract(ActionNode& action, bool full_compat) {
    action.input_size.reset();
    for (auto& param : action.configObject.params) {
        const std::string key = std::string(cosmo::util::Trim(param.key.ToString()));
        if (key == "inputSize") {
            InferenceInputSize size;
            if (action.input_size || !ParseInferenceInputSize(param.value.ToString(), size))
                return false;
            action.input_size = size;
            continue;
        }
        if (cosmo::key::FPS == key) {
            action.initFps = cosmo::util::ParseFloat(param.value.ToString());
            continue;
        }
        const bool is_atom_code_key =
            full_compat ? (key == cosmo::key::ATOM_CODE || key == "atomCode" || key == "atomic_code" ||
                           key == "atomiccode" || key == "modelCode" || key == "algorithm_code" ||
                           (key.size() > 10 && key.rfind(".atomicCode") == key.size() - 11))
                        : (key == cosmo::key::ATOM_CODE);
        if (is_atom_code_key) {
            action.atomAlgName = param.value.ToString();
            action.atomicCode  = param.value.ToString();
        }
    }
    return true;
}
}  // namespace cosmo::service::action_model_params
