#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include "nn/device/axera/axera_route_identity.h"

namespace cosmo::nn::axera::person720 {

// Retain the original scene and admit only a verified reusable pedestrian template.
inline bool IsSelected(const std::string& atomic, const std::string& scene, const std::string& semantic,
                       const std::string& model_sha256, bool trusted_person_template = false) {
    return atomic == "1001003" && !scene.empty() && (scene == "74470" || trusted_person_template) &&
           semantic == "ax650-nv12-1080p-pitch2048-bt709-limited-person-bgr-pad140-nms-v1" &&
           !model_sha256.empty() &&
           model_sha256 == cosmo::nn::axera::route_identity::Digest("person_720.sha256");
}

struct Box {
    float x1, y1, x2, y2;
};
inline bool Map1080To720(const Box& in, Box& out) {
    if (!std::isfinite(in.x1) || !std::isfinite(in.y1) || !std::isfinite(in.x2) || !std::isfinite(in.y2) ||
        in.x1 < 0 || in.y1 < 0 || in.x2 > 1920 || in.y2 > 1080 || in.x2 <= in.x1 || in.y2 <= in.y1)
        return false;
    constexpr float scale = 2.0f / 3.0f;
    out = {std::clamp(in.x1 * scale, 0.0f, 1280.0f), std::clamp(in.y1 * scale, 0.0f, 720.0f),
           std::clamp(in.x2 * scale, 0.0f, 1280.0f), std::clamp(in.y2 * scale, 0.0f, 720.0f)};
    return out.x2 > out.x1 && out.y2 > out.y1;
}
}  // namespace cosmo::nn::axera::person720
