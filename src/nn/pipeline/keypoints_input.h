#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "nn/core/blob.h"
#include "nn/core/status.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

// Validate and stage ROI metadata before executing the graph. Blob does not
// expose the backing allocation's actual byte length, which must be guaranteed
// separately by the caller.
Status CollectKeypointRectangles(const std::vector<std::shared_ptr<Blob>>& blobs, size_t max_batch,
                                 std::vector<Rect>& rectangles);

// The model must declare its coordinate width. A different runtime width
// cannot be safely interpreted as the model's landmark contract.
Status ValidateKeypointOutputColumns(const DimsVector& declared, const DimsVector& actual);

}  // namespace cosmo::nn
