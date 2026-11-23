#pragma once
// Reference .26 configuration, initial score mapping only; not a calibrated threshold.
#include <cmath>

#include "util/AiTypes.h"
namespace cosmo::reid_feature_task {
inline float CompareReference(const AiFeature& a, const AiFeature& b) noexcept {
    if (a.feature.size() != 1024 || b.feature.size() != 1024)
        return -1.f;
    float d = 0.f;
    for (std::size_t i = 0; i < 1024; ++i) {
        if (!std::isfinite(a.feature[i]) || !std::isfinite(b.feature[i]))
            return -1.f;
        const float x = a.feature[i] - b.feature[i];
        d += x * x;
    }
    if (!std::isfinite(d))
        return -1.f;
    constexpr float x = 0.3713f, y = 0.585f, z = 1.052f;
    if (d < 0.333f * x)
        return 100.f;
    if (d < x)
        return (1.5f * (x - d) / x * 0.2f + 0.8f) * 100.f;
    if (d <= y)
        return (0.8f - (d - x) / (y - x) * 0.2f) * 100.f;
    if (d <= z)
        return ((z - d) / (z - y) * 0.6f) * 100.f;
    return 0.f;
}
}  // namespace cosmo::reid_feature_task
