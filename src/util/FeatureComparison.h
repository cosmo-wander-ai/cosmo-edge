#pragma once

#include <vector>

#include "util/ErrorCode.h"

namespace cosmo::util {
// Calibrated 0-100 similarity, not a probability. Errors never produce a score.
ErrorEnum ComparePictureFeatures(const std::vector<float>& left, const std::vector<float>& right,
                                 const std::vector<float>& levels, bool face, double& score);
}  // namespace cosmo::util
