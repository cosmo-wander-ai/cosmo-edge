#include "util/SleepRoutePolicy.h"
#ifndef COSMO_AXERA_SLEEP_ROUTE_CANDIDATE
#error "sleep component requires explicit candidate gate"
#endif
#pragma once
// Descriptive validation of the observed model metadata; does not prove which
// model produced it. The loader must independently hash and bind model bytes.
#include <algorithm>
#include <cstring>
#include <initializer_list>

#include "HelmetMetadata.h"
namespace cosmo::helmet_candidate {
inline bool SleepMetadataMatches(const AX_ENGINE_IO_INFO_T* i) {
    if (!i || !i->pInputs || !i->pOutputs || i->nInputSize != 3 || i->nOutputSize != 2 ||
        i->nMaxBatchSize != 1 || i->bDynamicBatchSize)
        return false;
    return HelmetTensor(i->pInputs[0], "y_plane", AX_ENGINE_DT_UINT8, {1080, 2048}, 2211840, {2048, 1}) &&
           HelmetTensor(i->pInputs[1], "uv_plane", AX_ENGINE_DT_UINT8, {540, 2048}, 1105920, {2048, 1}) &&
           HelmetTensor(i->pInputs[2], "roi", AX_ENGINE_DT_SINT32, {4}, 16, {1}) &&
           HelmetTensor(i->pOutputs[0], "valid", AX_ENGINE_DT_UINT8, {1}, 1, {1}) &&
           HelmetTensor(i->pOutputs[1], "cls_output", AX_ENGINE_DT_FLOAT32, {1, 7}, 28, {7, 1});
}
}  // namespace cosmo::helmet_candidate
