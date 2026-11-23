#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace cosmo::native_fire {
// The atomic code is part of the repository's model catalog. Configuration
// expresses only intent; an independently bound contract is still required.
enum class Selection { NotFire, Selected, Invalid };
inline Selection Select(const std::string& atomic_code, const std::string& configured_fire_code) {
    constexpr const char* kFireCode = "2001003";
    if (atomic_code != kFireCode)
        return Selection::NotFire;
    return configured_fire_code == kFireCode ? Selection::Selected : Selection::Invalid;
}
// A selected fire route must also be present in the service's strict native
// set. Otherwise a detector could fall through to ordinary image processing.
inline Selection RequireStrict(const std::string& atomic_code, const std::string& configured_fire_code,
                               bool listed_as_strict) {
    const auto selection = Select(atomic_code, configured_fire_code);
    if (selection == Selection::Selected && !listed_as_strict)
        return Selection::Invalid;
    return selection;
}
// Compare the live detector labels against the immutable scene contract.
// Duplicate, unknown, disabled, missing, or bit-different thresholds fail closed.
template <typename Policies, typename Active>
inline bool MatchesPolicies(const Policies& policies, const Active& active) {
    if (policies.size() != 2 || policies[0].label != "fire" || policies[1].label != "smog" ||
        !(policies[0].enabled || policies[1].enabled))
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
// The legacy face status fields are specific to face; other strict routes
// must not report their capacity or overflow counter under that name.
inline bool ExposeFaceStatus(bool selected, const std::string& atomic_code) {
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
    if (atomic_code == "1001001")
        return false;
#endif
    return selected && atomic_code != "1001003" && atomic_code != "2001003" && atomic_code != "2000001";
}
}  // namespace cosmo::native_fire
