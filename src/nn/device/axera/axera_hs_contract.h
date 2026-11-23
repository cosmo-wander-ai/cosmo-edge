#pragma once
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) && !defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) &&            \
    !defined(COSMO_AXERA_HS_HOST_TEST) && !defined(COSMO_AXERA_HS_ROUTE_PRODUCTION)
#error "Hs candidate is limited to isolated device or host tests"
#endif
#if defined(COSMO_AXERA_HS_ROUTE_PRODUCTION) &&                                                              \
    (!defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) || defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) ||           \
     defined(COSMO_AXERA_HS_HOST_TEST) || defined(COSMO_AXERA_PERSON_TEST_SEAM) ||                           \
     defined(COSMO_AXERA_FACE_TEST_SEAM) || defined(COSMO_AXERA_FACE_ROUTE_TEST) ||                          \
     defined(COSMO_AXERA_FACE_HOSTKEY_TEST_SEAM))
#error "Production Hs cannot include isolated modes or test seams"
#endif
#if defined(COSMO_AXERA_HS_HOST_TEST) && defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST)
#error "Host test seam cannot be used in isolated device execution"
#endif

// Private, isolated HsDetection candidate; this binds only the measured model,
// not the independently deployed .26 model or an accuracy claim.
#include <algorithm>
#include <cmath>
#include <string>

#include "nn/device/axera/axera_route_identity.h"

namespace cosmo::nn::axera::hs_contract {
inline constexpr char kAtomicCode[]             = "1001001";
inline constexpr char kSourceClassLabel[]       = "HeadShoulder";
inline constexpr char kClassLabel[]             = "headShoulder";
inline constexpr char kReferenceMappingStatus[] = "unverified_reference_template";
inline constexpr char kSemantic[] = "ax650-nv12-1080p-pitch2048-bt709-limited-headshoulder-bgr-pad140-nms-v1";
inline const std::string& ModelSha256() {
    return cosmo::nn::axera::route_identity::Digest("head_shoulder.sha256");
}
inline const std::string& DeviceSmokeEvidenceSha256() {
    return cosmo::nn::axera::route_identity::Digest("head_shoulder.device_smoke_evidence_sha256");
}
inline const std::string& SourceOnnxSha256() {
    return cosmo::nn::axera::route_identity::Digest("head_shoulder.source_onnx_sha256");
}
inline const std::string& RuntimeGraphSha256() {
    return cosmo::nn::axera::route_identity::Digest("head_shoulder.runtime_graph_sha256");
}
inline constexpr float kSceneThreshold = .85f;
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
inline bool IsThreshold(float threshold) {
#ifdef COSMO_AXERA_HS_ROUTE_PRODUCTION
    return std::isfinite(threshold) && threshold >= .1f && threshold <= 1.f;
#else
    return std::isfinite(threshold) && threshold == kSceneThreshold;
#endif
}
}  // namespace cosmo::nn::axera::hs_contract
