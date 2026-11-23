#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "nn/device/axera/axera_hs_contract.h"

namespace cosmo::native_hs {
enum class Selection { NotHs, Selected, Invalid };
inline Selection RequireStrict(const std::string& atomic, bool listed_as_strict) {
    if (atomic != nn::axera::hs_contract::kAtomicCode)
        return Selection::NotHs;
    return listed_as_strict ? Selection::Selected : Selection::Invalid;
}
inline bool ManifestMatches(const std::string& name, const std::string& atomic, const std::string& scene) {
    return nn::axera::hs_contract::ManifestMatches(name, atomic, scene);
}
template <typename Active>
bool MatchesPolicy(const Active& active, float scene_threshold) {
    return active.size() == 1 && active[0].label == nn::axera::hs_contract::kClassLabel &&
           nn::axera::hs_contract::IsThreshold(scene_threshold) &&
           nn::axera::hs_contract::IsThreshold(active[0].confidence);
}
}  // namespace cosmo::native_hs
