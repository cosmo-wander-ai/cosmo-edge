#pragma once
#include <string>

#include "nn/device/axera/axera_route_identity.h"
namespace cosmo::nn::axera::jfif_fire_contract {
inline constexpr const char* kSemantic =
    "ax650-jfif-nv12-1080p-pitch2048-bt601-full-fire-smog-bgr736-pad161-k64-nms-v1";
// Replaced by the digest of the admitted conversion, before device testing.
inline const std::string& ModelSha256() {
    return cosmo::nn::axera::route_identity::Digest("fire_jfif.sha256");
}
inline bool Scene(const std::string& scene) {
    return scene == "92000009" || scene == "92000010";
}
}  // namespace cosmo::nn::axera::jfif_fire_contract
