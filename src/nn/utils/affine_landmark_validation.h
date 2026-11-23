#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>

#include "nn/core/blob.h"
#include "nn/core/status.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

// The current Sophon implementation executes exactly one image and one
// matrix; accepting a larger batch would leave remaining outputs unwritten.
inline Status ValidateSophonAffineBatch(size_t image_count, size_t landmark_groups, int landmark_count) {
    if (image_count != 1 || landmark_groups != 1 || landmark_count != 1)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Sophon affine supports one image and one ROI");
    return Status{COSMO_NN_OK};
}

// Face affine alignment requires exactly five interleaved x/y coordinates.
// Blob does not expose its backing allocation length; callers must guarantee
// that the declared packed tensor is backed by readable host memory.
inline Status ValidateAffineLandmarkBlob(const std::shared_ptr<Blob>& blob, int max_batch) {
    if (!blob || max_batch <= 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid affine landmark blob or batch limit");
    const auto& desc = blob->GetBlobDesc();
    if (desc.data_type != DATA_TYPE_FLOAT || desc.dims.size() != 2 || desc.dims[0] <= 0 ||
        desc.dims[0] > max_batch || desc.dims[1] != 10 || DimsVectorUtils::Count(desc.dims) <= 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Affine landmarks require a [batch, 10] float tensor");
    const auto* bytes = static_cast<const unsigned char*>(blob->GetHandle().base);
    if (!bytes)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Affine landmark buffer is null");
    const auto count = static_cast<size_t>(desc.dims[0]) * 10;
    for (size_t i = 0; i < count; ++i) {
        float coordinate;
        std::memcpy(&coordinate, bytes + i * sizeof(float), sizeof(coordinate));
        if (!std::isfinite(coordinate) || static_cast<double>(coordinate) < std::numeric_limits<int>::min() ||
            static_cast<double>(coordinate) > std::numeric_limits<int>::max())
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid affine landmark coordinate");
    }
    return Status{COSMO_NN_OK};
}

}  // namespace cosmo::nn
