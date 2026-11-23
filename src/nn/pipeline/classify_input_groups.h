#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "nn/core/blob.h"
#include "nn/core/status.h"

namespace cosmo::nn {
using ClassifyInputGroups = std::vector<std::vector<std::shared_ptr<Blob>>>;

// Graph crop branches consume one image group and one ROI group each.
// Only the independent, uniformly cropped fusion case is expanded here.
template <typename Forward>
Status RunClassifyInputGroups(std::initializer_list<std::vector<std::shared_ptr<Blob>>> inputs,
                              const std::string& reduce, const std::vector<int>& branch_batches,
                              bool all_single_crop, Forward forward) {
    ClassifyInputGroups groups(inputs);
    const bool fused = branch_batches.size() > 1 && (reduce == "mean" || reduce == "concat");
    if (!fused || !all_single_crop || groups.size() != 2)
        return forward(groups);

    const int max_batch = branch_batches.front();
    if (max_batch <= 0)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid classifier branch batch size");
    for (const int batch : branch_batches) {
        if (batch != max_batch)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Classifier branch batch sizes differ");
    }
    if (groups[0].empty() || groups[0].size() != groups[1].size())
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Classifier image and ROI groups differ");
    size_t count = 0;
    for (size_t i = 0; i < groups[0].size(); ++i) {
        const auto& image = groups[0][i];
        const auto& roi   = groups[1][i];
        if (!image || !roi)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Null classifier image or ROI");
        const auto& desc = roi->GetBlobDesc();
        if (desc.data_type != DATA_TYPE_INT32 || desc.dims.size() != 2 || desc.dims[0] <= 0 ||
            desc.dims[1] != 4)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid classifier ROI structure");
        const auto rows = static_cast<size_t>(desc.dims[0]);
        if (rows > static_cast<size_t>(max_batch) - count)
            return Status(COSMO_NN_ERR_INVALID_INPUT, "Classifier ROI batch too large");
        count += rows;
    }
    if (branch_batches.size() > groups.max_size() / 2)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Too many classifier branches");
    ClassifyInputGroups expanded;
    expanded.reserve(branch_batches.size() * 2);
    for (size_t i = 0; i < branch_batches.size(); ++i) {
        expanded.push_back(groups[0]);
        expanded.push_back(groups[1]);
    }
    return forward(expanded);
}
}  // namespace cosmo::nn
