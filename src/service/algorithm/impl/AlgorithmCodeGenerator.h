// Algorithm unique code generator — extracted from AlgorithmServiceImpl::AddFromJson

#pragma once

#include <chrono>
#include <string>
#include <unordered_set>

#include "util/InferConstants.h"

namespace cosmo::service::detail {

/// Generate a 5-digit numeric code based on timestamp that does not conflict with existingIds
/// @param seedOffset  Seed offset for generating multiple distinct codes at the same timestamp
/// @param existingIds Set of already occupied IDs
/// @return Generated code string; empty string on failure
inline std::string GenerateAlgorithmCode(int seedOffset, const std::unordered_set<std::string>& existingIds) {
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    int startVal = static_cast<int>((nowMs + seedOffset) % 100000);
    for (int i = 0; i < 100000; ++i) {
        int candidate = (startVal + i) % 100000;
        if (candidate == kReservedAlgorithmCode)
            continue;
        std::string code = std::to_string(candidate);
        if (existingIds.find(code) == existingIds.end())
            return code;
    }
    return std::string{};
}

}  // namespace cosmo::service::detail
