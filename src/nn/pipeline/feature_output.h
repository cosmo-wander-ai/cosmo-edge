#pragma once

#include <memory>
#include <vector>

#include "nn/core/blob.h"
#include "nn/core/status.h"

namespace cosmo::nn {

// Produces normalized features only if the entire output tensor is valid.
Status ParseFeatureOutputBlobs(const std::vector<std::shared_ptr<Blob>>& blobs,
                               std::vector<std::vector<float>>& outputs, size_t expected_batch = 0,
                               size_t max_batch = 0, bool allow_fixed_padding = false);

}  // namespace cosmo::nn
