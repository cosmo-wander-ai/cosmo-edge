#pragma once

// Result adaptation for the experimental 512x512 face graph with a centered
// 512x288 content region. This does not execute the graph or establish device
// completion, memory visibility, model identity, or frame ownership.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <vector>

#include "nn/device/axera/detections_v1.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn::axera {

enum class FaceResultError {
    None,
    InvalidGeometry,
    InvalidThreshold,
    InvalidTensor,
    InvalidClass,
    AllocationFailure
};

struct FaceResult {
    FaceResultError error{FaceResultError::None};
    detections_v1::Error tensor_error{detections_v1::Error::None};
    std::vector<ObjectInfoV1> objects;
};

// source_width/height must describe the same leased frame used for inference.
// The caller must establish that this exact graph, padding and tensor contract
// belong together before calling. An overflow is an error, never a truncated
// successful frame. Only compact result metadata is read on the host.
inline FaceResult ConvertFaceResult(const detections_v1::Contract& contract,
                                    const detections_v1::TensorView& tensor, int source_width,
                                    int source_height, float threshold) {
    auto fail = [](FaceResultError error, detections_v1::Error tensor_error = detections_v1::Error::None) {
        return FaceResult{error, tensor_error, {}};
    };
    if (source_width <= 0 || source_height <= 0 ||
        static_cast<std::uint64_t>(source_width) * 9 != static_cast<std::uint64_t>(source_height) * 16)
        return fail(FaceResultError::InvalidGeometry);
    if (!std::isfinite(threshold) || threshold < 0.f || threshold > 1.f ||
        !std::isfinite(contract.confidence) || threshold < contract.confidence)
        return fail(FaceResultError::InvalidThreshold);
    if (contract.class_count != 1)
        return fail(FaceResultError::InvalidClass);

    auto parsed = detections_v1::Parse(contract, tensor);
    if (parsed.error != detections_v1::Error::None)
        return fail(FaceResultError::InvalidTensor, parsed.error);

    try {
        std::vector<ObjectInfoV1> staged;
        staged.reserve(parsed.detections.size());
        const double scale = static_cast<double>(source_width) / 512.;
        for (const auto& detection : parsed.detections) {
            if (detection.class_id != 0)
                return fail(FaceResultError::InvalidClass);
            if (detection.score < threshold)
                continue;
            const auto x1 = static_cast<float>(std::clamp((detection.cx - detection.width * 0.5) * scale, 0.,
                                                          static_cast<double>(source_width)));
            const auto y1 =
                static_cast<float>(std::clamp((detection.cy - detection.height * 0.5 - 112.) * scale, 0.,
                                              static_cast<double>(source_height)));
            const auto x2 = static_cast<float>(std::clamp((detection.cx + detection.width * 0.5) * scale, 0.,
                                                          static_cast<double>(source_width)));
            const auto y2 =
                static_cast<float>(std::clamp((detection.cy + detection.height * 0.5 - 112.) * scale, 0.,
                                              static_cast<double>(source_height)));
            if (!(x2 > x1 && y2 > y1))
                continue;
            ObjectInfoV1 object;
            object.x1 = x1;
            object.y1 = y1;
            object.x2 = x2;
            object.y2 = y2;
            object.infos.push_back({detection.score, 0, "face"});
            staged.push_back(std::move(object));
        }
        return {FaceResultError::None, detections_v1::Error::None, std::move(staged)};
    } catch (const std::bad_alloc&) {
        return fail(FaceResultError::AllocationFailure);
    } catch (const std::length_error&) {
        return fail(FaceResultError::AllocationFailure);
    }
}

}  // namespace cosmo::nn::axera
