#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "nn/utils/net_utils.h"

namespace cosmo {

// The classification parser produces one object per ROI. Sophon's fixed
// output stage can also return trailing entries up to the configured batch.
inline bool ValidateClassifierResults(std::vector<std::vector<nn::ObjectInfoV1>>& outputs, size_t expected,
                                      size_t max_batch, bool allow_fixed_padding) {
    const bool exact  = outputs.size() == expected;
    const bool padded = allow_fixed_padding && expected < max_batch && outputs.size() == max_batch;
    if ((!exact && !padded) ||
        !std::all_of(outputs.begin(), outputs.end(), [](const auto& output) { return output.size() == 1; }))
        return false;
    if (padded)
        outputs.resize(expected);
    return true;
}

}  // namespace cosmo
