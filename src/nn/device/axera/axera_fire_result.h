#pragma once

// Experimental metadata-only adapter for the hash-bound fire/smog graph.
// The caller must independently bind the model and same-frame native lease.
#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include "nn/device/axera/axera_fire_model_binding.h"
#include "nn/device/axera/detections_v1.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn::axera {

enum class FireResultError {
    None,
    InvalidGeometry,
    InvalidContract,
    InvalidPolicy,
    InvalidTensor,
    AllocationFailure
};

struct FireResult {
    FireResultError error{FireResultError::None};
    detections_v1::Error tensor_error{detections_v1::Error::None};
    std::vector<ObjectInfoV1> objects;
};

inline FireResult ConvertFireResult(const detections_v1::Contract& contract,
                                    const detections_v1::TensorView& tensor, int source_width,
                                    int source_height,
                                    const std::array<fire_binding::ClassPolicy, 2>& policies) {
    auto fail = [](FireResultError error, detections_v1::Error tensor_error = detections_v1::Error::None) {
        return FireResult{error, tensor_error, {}};
    };
    if (source_width != 1920 || source_height != 1080)
        return fail(FireResultError::InvalidGeometry);
    if (contract.capacity != 64 || contract.class_count != 2 ||
        !detections_v1::SameBits(contract.confidence, .1f) || !detections_v1::SameBits(contract.iou, .35f))
        return fail(FireResultError::InvalidContract);
    if (policies[0].label != "fire" || policies[1].label != "smog" ||
        !(policies[0].enabled || policies[1].enabled) ||
        !std::all_of(policies.begin(), policies.end(), [&](const fire_binding::ClassPolicy& policy) {
            return std::isfinite(policy.threshold) && policy.threshold >= contract.confidence &&
                   policy.threshold <= 1.f;
        }))
        return fail(FireResultError::InvalidPolicy);
    const auto parsed = detections_v1::Parse(contract, tensor);
    if (parsed.error != detections_v1::Error::None)
        return fail(FireResultError::InvalidTensor, parsed.error);

    try {
        std::vector<ObjectInfoV1> staged;
        staged.reserve(parsed.detections.size());
        for (const auto& detection : parsed.detections) {
            const auto& policy = policies[static_cast<std::size_t>(detection.class_id)];
            if (!policy.enabled || detection.score < policy.threshold)
                continue;
            // 1920x1080 -> 736x414, centered at y=161 in the 736 square.
            constexpr double scale = 60.0 / 23.0;
            const double cx = detection.cx, cy = detection.cy;
            const double width = detection.width, height = detection.height;
            const auto x1 = static_cast<float>(std::clamp((cx - width * .5) * scale, 0., 1920.));
            const auto x2 = static_cast<float>(std::clamp((cx + width * .5) * scale, 0., 1920.));
            const auto y1 = static_cast<float>(std::clamp((cy - height * .5 - 161.) * scale, 0., 1080.));
            const auto y2 = static_cast<float>(std::clamp((cy + height * .5 - 161.) * scale, 0., 1080.));
            if (!(x2 > x1 && y2 > y1))
                continue;
            ObjectInfoV1 object;
            object.x1 = x1;
            object.y1 = y1;
            object.x2 = x2;
            object.y2 = y2;
            object.infos.push_back({detection.score, detection.class_id, policy.label});
            staged.push_back(std::move(object));
        }
        return {FireResultError::None, detections_v1::Error::None, std::move(staged)};
    } catch (const std::bad_alloc&) {
        return fail(FireResultError::AllocationFailure);
    } catch (const std::length_error&) {
        return fail(FireResultError::AllocationFailure);
    }
}

}  // namespace cosmo::nn::axera
