#pragma once
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) && !defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) &&       \
    !defined(COSMO_AXERA_RUBBISH_HOST_TEST) && !defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION)
#error "Rubbish route requires isolated test or explicit production build"
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION) &&                                                         \
    (!defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) || defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) ||      \
     defined(COSMO_AXERA_RUBBISH_HOST_TEST) || defined(COSMO_AXERA_PERSON_TEST_SEAM) ||                      \
     defined(COSMO_AXERA_FACE_TEST_SEAM))
#error "Rubbish production route cannot include test seams"
#endif
#include <algorithm>
#include <cmath>
#include <string>

#include "nn/device/axera/axera_route_identity.h"
namespace cosmo::nn::axera::rubbish_contract {
inline constexpr char kAtomicCode[]             = "2001008";
inline constexpr char kSourceClassLabel[]       = "rubbishdump";
inline constexpr char kClassLabel[]             = "rubbishDump";
inline constexpr char kReferenceMappingStatus[] = "unverified_reference_template";
inline constexpr char kSemantic[] = "ax650-nv12-1080p-pitch2048-bt709-limited-rubbish-bgr-pad140-nms-v1";
inline const std::string& ModelSha256() {
    return cosmo::nn::axera::route_identity::Digest("rubbish.sha256");
}
inline const std::string& SourceOnnxSha256() {
    return cosmo::nn::axera::route_identity::Digest("rubbish.source_onnx_sha256");
}
inline const std::string& RuntimeGraphSha256() {
    return cosmo::nn::axera::route_identity::Digest("rubbish.runtime_graph_sha256");
}
inline const std::string& DeviceSmokeEvidenceSha256() {
    return cosmo::nn::axera::route_identity::Digest("rubbish.device_smoke_evidence_sha256");
}
inline constexpr float kSceneThreshold = .76f;
inline bool IsCandidate(const std::string& atomic, const std::string& semantic, const std::string& hash) {
    return atomic == kAtomicCode && semantic == kSemantic && !hash.empty() && hash == ModelSha256();
}
inline bool SafeScene(const std::string& scene) {
    return !scene.empty() && scene.size() <= 120 && scene != "." && scene != ".." &&
           std::all_of(scene.begin(), scene.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-' || c == '.';
           });
}
inline bool ManifestMatches(const std::string& name, const std::string& atomic, const std::string& scene) {
    return atomic == kAtomicCode && SafeScene(scene) && name == atomic + "-" + scene + ".json";
}
inline bool IsThreshold(float value) {
    return std::isfinite(value) && value >= .1f && value <= 1.f;
}
}  // namespace cosmo::nn::axera::rubbish_contract
