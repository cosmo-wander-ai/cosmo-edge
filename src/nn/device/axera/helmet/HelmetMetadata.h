#pragma once
// Descriptive validation of the observed model metadata; does not prove which
// model produced it. The loader must independently hash and bind model bytes.
#include <algorithm>
#include <cstring>
#include <initializer_list>

#include "ax_engine_api.h"
namespace cosmo::helmet_candidate {
inline bool HelmetTensor(const AX_ENGINE_IOMETA_T& m, const char* name, AX_ENGINE_DATA_TYPE_T type,
                         std::initializer_list<AX_S32> shape, AX_U32 bytes,
                         std::initializer_list<AX_S32> strides) {
    return m.pName && std::strcmp(m.pName, name) == 0 && m.pShape && m.nShapeSize == shape.size() &&
           m.eDataType == type && static_cast<int>(m.eLayout) == 0 && static_cast<int>(m.eMemoryType) == 0 &&
           m.pExtraMeta && static_cast<int>(m.pExtraMeta->eColorSpace) == 0 && m.nQuantizationValue == 0 &&
           m.nSize == bytes && std::equal(shape.begin(), shape.end(), m.pShape) &&
           (!m.pStride || std::equal(strides.begin(), strides.end(), m.pStride));
}
inline bool HelmetMetadataMatches(const AX_ENGINE_IO_INFO_T* i) {
    if (!i || !i->pInputs || !i->pOutputs || i->nInputSize != 3 || i->nOutputSize != 2 ||
        i->nMaxBatchSize != 1 || i->bDynamicBatchSize)
        return false;
    return HelmetTensor(i->pInputs[0], "y_plane", AX_ENGINE_DT_UINT8, {1080, 2048}, 2211840, {2048, 1}) &&
           HelmetTensor(i->pInputs[1], "uv_plane", AX_ENGINE_DT_UINT8, {540, 2048}, 1105920, {2048, 1}) &&
           HelmetTensor(i->pInputs[2], "roi", AX_ENGINE_DT_SINT32, {4}, 16, {1}) &&
           HelmetTensor(i->pOutputs[0], "valid", AX_ENGINE_DT_UINT8, {1}, 1, {1}) &&
           HelmetTensor(i->pOutputs[1], "cls_output", AX_ENGINE_DT_FLOAT32, {1, 5}, 20, {5, 1});
}
}  // namespace cosmo::helmet_candidate
