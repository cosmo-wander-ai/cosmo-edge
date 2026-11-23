#pragma once

// Metadata-only conversion for the candidate 640x640 pedestrian graph. The
// caller must independently bind this contract to the loaded model and to the
// completed native frame; this helper grants neither permission nor execution.
#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include "nn/device/axera/axera_hs_contract.h"
#include "nn/device/axera/detections_v1.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn::axera {

enum class PersonResultError {
    None,
    InvalidGeometry,
    InvalidContract,
    InvalidThreshold,
    InvalidTensor,
    AllocationFailure
};

struct PersonResult {
    PersonResultError error{PersonResultError::None};
    detections_v1::Error tensor_error{detections_v1::Error::None};
    std::vector<ObjectInfoV1> objects;
};

inline PersonResult ConvertPersonResult(const detections_v1::Contract& contract,
                                        const detections_v1::TensorView& tensor, int source_width,
                                        int source_height, float threshold) {
    auto fail = [](PersonResultError error, detections_v1::Error tensor_error = detections_v1::Error::None) {
        return PersonResult{error, tensor_error, {}};
    };
    if (source_width != 1920 || source_height != 1080)
        return fail(PersonResultError::InvalidGeometry);
    if (contract.capacity != 64 || contract.class_count != 1 ||
        !detections_v1::SameBits(contract.confidence, .1f) || !detections_v1::SameBits(contract.iou, .35f))
        return fail(PersonResultError::InvalidContract);
    if (!std::isfinite(threshold) || threshold < contract.confidence || threshold > 1.f)
        return fail(PersonResultError::InvalidThreshold);
    const auto parsed = detections_v1::Parse(contract, tensor);
    if (parsed.error != detections_v1::Error::None)
        return fail(PersonResultError::InvalidTensor, parsed.error);

    try {
        std::vector<ObjectInfoV1> staged;
        staged.reserve(parsed.detections.size());
        for (const auto& detection : parsed.detections) {
            if (detection.score < threshold)
                continue;
            // 1920x1080 -> 640x360, centered vertically in 640x640.
            const double cx = detection.cx, cy = detection.cy;
            const double width = detection.width, height = detection.height;
            const auto x1 = static_cast<float>(std::clamp((cx - width * .5) * 3., 0., 1920.));
            const auto x2 = static_cast<float>(std::clamp((cx + width * .5) * 3., 0., 1920.));
            const auto y1 = static_cast<float>(std::clamp((cy - height * .5 - 140.) * 3., 0., 1080.));
            const auto y2 = static_cast<float>(std::clamp((cy + height * .5 - 140.) * 3., 0., 1080.));
            if (!(x2 > x1 && y2 > y1))
                continue;
            ObjectInfoV1 object;
            object.x1 = x1;
            object.y1 = y1;
            object.x2 = x2;
            object.y2 = y2;
            object.infos.push_back({detection.score, 0, "pedestrian"});
            staged.push_back(std::move(object));
        }
        return {PersonResultError::None, detections_v1::Error::None, std::move(staged)};
    } catch (const std::bad_alloc&) {
        return fail(PersonResultError::AllocationFailure);
    } catch (const std::length_error&) {
        return fail(PersonResultError::AllocationFailure);
    }
}

// Reuses the validated single-class K64 parser. Overflow/invalid tensors fail
// before threshold filtering, so a truncated head-shoulder frame emits nothing.
inline PersonResult ConvertHsResult(const detections_v1::Contract& contract,
                                    const detections_v1::TensorView& tensor, int source_width,
                                    int source_height, float threshold) {
    if (!hs_contract::IsThreshold(threshold))
        return {PersonResultError::InvalidThreshold, detections_v1::Error::None, {}};
    auto result = ConvertPersonResult(contract, tensor, source_width, source_height, threshold);
    if (result.error != PersonResultError::None)
        return result;
    try {
        for (auto& object : result.objects)
            object.infos.at(0).class_name = hs_contract::kClassLabel;
        return result;
    } catch (...) {
        return {PersonResultError::AllocationFailure, detections_v1::Error::None, {}};
    }
}

}  // namespace cosmo::nn::axera
