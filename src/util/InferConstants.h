// Sentinel and limit constants for the inference subsystem.
#pragma once

#include <cstdint>
#include <limits>

namespace cosmo {

// Sentinel value indicating "no valid distance computed" in group-target analysis.
constexpr int kNoDistanceSentinel = std::numeric_limits<int>::max();

// Sentinel value returned when feature vectors have mismatched dimensions.
constexpr float kFeatureDimensionMismatch = std::numeric_limits<float>::max();

}  // namespace cosmo

namespace cosmo::service::detail {

// Reserved algorithm code that must never be assigned to user algorithms.
constexpr int kReservedAlgorithmCode = 10000;

}  // namespace cosmo::service::detail
