#pragma once
#include "FaceModelBundle.h"
// Bind the model bundle and its color profile as one operation, so a caller
// cannot feed JPEG pixels into the video preprocessing model or vice versa.
namespace cosmo::helmet_candidate {
inline FaceChainResult ExecuteFaceTask(std::unique_ptr<PendingResources> pending,
                                       const std::shared_ptr<FaceModelBundle>& bundle,
                                       const std::vector<std::array<std::int32_t, 4>>& rois) {
    if (!pending || !bundle)
        return {};
    auto owner = std::make_shared<FaceChainOwner>();
    for (std::size_t i = 0; i < 4; ++i) {
        owner->models[i] = bundle->models[i + 1];
        if (!owner->models[i])
            return {};
        owner->handles[i] = owner->models[i]->HandleForOwnerThread();
        if (!owner->handles[i])
            return {};
    }
    pending->model = bundle->models[0];
    if (!pending->model)
        return {};
    pending->handle = pending->model->HandleForOwnerThread();
    if (!pending->handle)
        return {};
    pending->io_owner = std::move(owner);
    return ExecuteFaceChainSdkBatch(std::move(pending), rois, bundle->profile);
}
}  // namespace cosmo::helmet_candidate
