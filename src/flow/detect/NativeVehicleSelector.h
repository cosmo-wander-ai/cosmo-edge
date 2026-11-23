#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace cosmo::native_vehicle {
// An environment selection expresses only route intent; it cannot authorize
// model bytes, scene semantics, frame provenance or capacity.
enum class Selection { NotVehicle, Selected, Invalid };
inline Selection RequireStrict(const std::string& atomic_code, bool listed_as_strict) {
    if (atomic_code != "2000001")
        return Selection::NotVehicle;
    return listed_as_strict ? Selection::Selected : Selection::Invalid;
}
// Detector labels must be the exact enabled policies in the immutable scene
// contract. Do not accept duplicated/unknown labels or threshold rounding.
template <typename Policies, typename Active>
inline bool MatchesPolicies(const Policies& policies, const Active& active) {
    if (policies.size() != 3 || policies[0].label != "Person" || policies[1].label != "Nonvehicle" ||
        policies[2].label != "Vehicle" ||
        !(policies[0].enabled || policies[1].enabled || policies[2].enabled))
        return false;
    std::size_t expected = 0;
    for (const auto& policy : policies) {
        if (!std::isfinite(policy.threshold) || policy.threshold < .1f || policy.threshold > 1.f)
            return false;
        const auto count = std::count_if(active.begin(), active.end(),
                                         [&](const auto& value) { return value.label == policy.label; });
        if (count != static_cast<decltype(count)>(policy.enabled))
            return false;
        if (policy.enabled) {
            ++expected;
            const auto found = std::find_if(active.begin(), active.end(),
                                            [&](const auto& value) { return value.label == policy.label; });
            if (std::memcmp(&found->confidence, &policy.threshold, sizeof(float)) != 0)
                return false;
        }
    }
    return active.size() == expected;
}
}  // namespace cosmo::native_vehicle
