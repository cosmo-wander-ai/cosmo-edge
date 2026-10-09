#include "util/FeatureComparison.h"

#include <algorithm>
#include <cmath>

namespace cosmo::util {
ErrorEnum ComparePictureFeatures(const std::vector<float>& left, const std::vector<float>& right,
                                 const std::vector<float>& levels, bool face, double& score) {
    if (left.empty() || left.size() != right.size())
        return ErrorEnum::PicturePairInvalidFeature;
    double dot = 0, normA = 0, normB = 0, distance = 0;
    for (size_t i = 0; i < left.size(); ++i) {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
            return ErrorEnum::PicturePairInvalidFeature;
        const double a = left[i], b = right[i];
        dot += a * b;
        normA += a * a;
        normB += b * b;
        distance += (a - b) * (a - b);
    }
    if (normA <= 0 || normB <= 0)
        return ErrorEnum::PicturePairInvalidFeature;
    if (levels.size() < 3 || !std::isfinite(levels[0]) || !std::isfinite(levels[1]) ||
        !std::isfinite(levels[2]) || !(0 < levels[0] && levels[0] < levels[1] && levels[1] < levels[2]))
        return ErrorEnum::PicturePairInvalidCalibration;
    if (face)
        distance = 2 * (1 - std::clamp(dot / std::sqrt(normA * normB), -1.0, 1.0));
    const double low = levels[0], middle = levels[1], high = levels[2];
    if (distance < low * 0.333)
        score = 100;
    else if (distance < low)
        score = (1.5 * (low - distance) / low * 0.2 + 0.8) * 100;
    else if (distance <= middle)
        score = (0.8 - (distance - low) / (middle - low) * 0.2) * 100;
    else if (distance <= high)
        score = (high - distance) / (high - middle) * 60;
    else
        score = 0;
    score = std::clamp(score, 0.0, 100.0);
    return ErrorEnum::Success;
}
}  // namespace cosmo::util
