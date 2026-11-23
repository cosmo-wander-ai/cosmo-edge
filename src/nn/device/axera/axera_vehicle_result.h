#pragma once

// Offline candidate only. Requires an independent, byte-hash-bound compiled
// vehicle model, same-frame native lease and device-completion proof. No CPU
// image processing, model-head decoding or NMS is performed here.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nn/device/axera/axera_vehicle_model_binding.h"
#include "nn/device/axera/detections_v1.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn::axera {

using VehicleClassPolicy = vehicle_binding::ClassPolicy;
enum class VehicleResultError {
    None,
    InvalidGeometry,
    InvalidContract,
    InvalidPolicy,
    InvalidTensor,
    AllocationFailure
};
struct VehicleResult {
    VehicleResultError error{VehicleResultError::None};
    detections_v1::Error tensor_error{detections_v1::Error::None};
    std::vector<ObjectInfoV1> objects;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    bool diagnostic_raw_valid{false};
    std::array<std::uint64_t, 3> diagnostic_raw_counts{};
    std::array<unsigned char, 32> diagnostic_tensor_sha256{};
#endif
};

inline VehicleResult ConvertVehicleResult(const detections_v1::Contract& contract,
                                          const detections_v1::TensorView& tensor, int source_width,
                                          int source_height,
                                          const std::array<VehicleClassPolicy, 3>& policies) {
    auto fail = [](VehicleResultError error, detections_v1::Error tensor_error = detections_v1::Error::None) {
        return VehicleResult{error, tensor_error, {}};
    };
    if (source_width != 1920 || source_height != 1080)
        return fail(VehicleResultError::InvalidGeometry);
    if (contract.capacity != 64 || contract.class_count != 3 ||
        !detections_v1::SameBits(contract.confidence, .1f) || !detections_v1::SameBits(contract.iou, .35f))
        return fail(VehicleResultError::InvalidContract);
    if (policies[0].label != "Person" || policies[1].label != "Nonvehicle" ||
        policies[2].label != "Vehicle" ||
        !std::any_of(policies.begin(), policies.end(), [](const auto& p) { return p.enabled; }) ||
        !std::all_of(policies.begin(), policies.end(), [&](const auto& p) {
            return std::isfinite(p.threshold) && p.threshold >= contract.confidence && p.threshold <= 1.f;
        }))
        return fail(VehicleResultError::InvalidPolicy);
    const auto parsed = detections_v1::Parse(contract, tensor);
    if (parsed.error != detections_v1::Error::None)
        return fail(VehicleResultError::InvalidTensor, parsed.error);
    try {
        std::vector<ObjectInfoV1> objects;
        objects.reserve(parsed.detections.size());
        for (const auto& detection : parsed.detections) {
            const auto& policy = policies[static_cast<std::size_t>(detection.class_id)];
            if (!policy.enabled || detection.score < policy.threshold)
                continue;
            // Candidate input fit: 1920x1080 -> 640x360; 140-pixel vertical pad.
            const double cx = detection.cx, cy = detection.cy;
            const double w = detection.width, h = detection.height;
            const float x1 = static_cast<float>(std::clamp((cx - w * .5) * 3., 0., 1920.));
            const float x2 = static_cast<float>(std::clamp((cx + w * .5) * 3., 0., 1920.));
            const float y1 = static_cast<float>(std::clamp((cy - h * .5 - 140.) * 3., 0., 1080.));
            const float y2 = static_cast<float>(std::clamp((cy + h * .5 - 140.) * 3., 0., 1080.));
            if (!(x2 > x1 && y2 > y1))
                continue;
            ObjectInfoV1 object;
            object.x1 = x1;
            object.y1 = y1;
            object.x2 = x2;
            object.y2 = y2;
            object.infos.push_back({detection.score, detection.class_id, policy.label});
            objects.push_back(std::move(object));
        }
        VehicleResult result{VehicleResultError::None, detections_v1::Error::None, std::move(objects)};
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        // Validated raw NPU detections before class-policy filtering.
        for (const auto& detection : parsed.detections)
            ++result.diagnostic_raw_counts[static_cast<std::size_t>(detection.class_id)];
#endif
        return result;
    } catch (const std::bad_alloc&) {
        return fail(VehicleResultError::AllocationFailure);
    } catch (const std::length_error&) {
        return fail(VehicleResultError::AllocationFailure);
    }
}
}  // namespace cosmo::nn::axera
