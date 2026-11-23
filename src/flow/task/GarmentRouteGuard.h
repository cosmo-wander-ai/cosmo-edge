#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "flow/detect/NativeModelSelector.h"
#include "util/dto/AlgorithmMsgTypes.h"
namespace cosmo::garment_guard {
// Scene 88111 is an AX native-only cascade. No selector or graph means reject
// before creating actions; admission, frame leases and model identity remain
// separate checks at TaskStart.
inline bool CreateAllowed(const std::string& scene, bool supported,
                          const native_face::RequiredCodes& required,
                          const std::vector<ActionNode>& actions) {
    if (scene != "88115" && scene != "88116")
        return true;
    if (!supported)
        return false;
    if (!required.valid ||
        std::find(required.codes.begin(), required.codes.end(), "1001003") == required.codes.end())
        return false;
    unsigned detectors = 0, classifiers = 0;
    for (const auto& action : actions) {
        if (action.actionId == "AA_00001") {
            ++detectors;
            if (action.atomicCode != "1001003")
                return false;
        }
        if (action.actionId == "AA_00002") {
            ++classifiers;
            if (action.atomicCode != "1001005")
                return false;
        }
    }
    return detectors == 1 && classifiers == 1;
}
}  // namespace cosmo::garment_guard
