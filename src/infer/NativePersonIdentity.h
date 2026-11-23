#pragma once
#include <string>

#include "nn/device/axera/axera_hs_contract.h"
#include "nn/device/axera/axera_mouse_contract.h"
#include "nn/device/axera/axera_rubbish_contract.h"
namespace cosmo::native_person_identity {
inline const char* Select(const std::string& atomic, const std::string& semantic, const std::string& hash) {
    if (atomic == "1001003" && semantic == "ax650-nv12-1080p-pitch2048-bt709-limited-person-bgr-pad140-nms-v1"
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_HOST_TEST) ||                  \
     defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
        && hash != nn::axera::hs_contract::ModelSha256()
#endif
    )
        return "pedestrian";
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) &&                                                               \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_HS_HOST_TEST) ||                  \
     defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
    if (nn::axera::hs_contract::IsCandidate(atomic, semantic, hash))
        return nn::axera::hs_contract::kClassLabel;
#endif
#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) &&                                                            \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_MOUSE_HOST_TEST) ||               \
     defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION))
    if (nn::axera::mouse_contract::IsCandidate(atomic, semantic, hash))
        return nn::axera::mouse_contract::kClassLabel;
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) &&                                                          \
    (defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) || defined(COSMO_AXERA_RUBBISH_HOST_TEST) ||             \
     defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION))
    if (nn::axera::rubbish_contract::IsCandidate(atomic, semantic, hash))
        return nn::axera::rubbish_contract::kClassLabel;
#endif
    return nullptr;
}
}  // namespace cosmo::native_person_identity
