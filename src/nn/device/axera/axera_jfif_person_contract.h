#pragma once
#include <string>

#include "nn/device/axera/axera_route_identity.h"
namespace cosmo::nn::axera::jfif_person_contract {
inline constexpr const char* kSemantic =
    "ax650-jfif-nv12-1080p-pitch2048-bt601-full-person-bgr640-pad140-k64-nms-v1";
inline const std::string& ModelSha256() {
    return cosmo::nn::axera::route_identity::Digest("person_jfif.sha256");
}
inline bool Scene(const std::string& id) {
    return id == "92000011";
}
}  // namespace cosmo::nn::axera::jfif_person_contract
