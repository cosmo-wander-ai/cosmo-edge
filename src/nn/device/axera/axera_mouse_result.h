#pragma once
#include "nn/device/axera/axera_mouse_contract.h"
#include "nn/device/axera/axera_person_result.h"
namespace cosmo::nn::axera {
inline PersonResult ConvertMouseResult(const detections_v1::Contract& contract,
                                       const detections_v1::TensorView& tensor, int source_width,
                                       int source_height, float threshold) {
    auto fail = [](PersonResultError e, detections_v1::Error t = detections_v1::Error::None) {
        return PersonResult{e, t, {}};
    };
    if (source_width != 1920 || source_height != 1080)
        return fail(PersonResultError::InvalidGeometry);
    if (contract.capacity != 64 || contract.class_count != 1 ||
        !detections_v1::SameBits(contract.confidence, .1f) || !detections_v1::SameBits(contract.iou, .35f))
        return fail(PersonResultError::InvalidContract);
    if (!mouse_contract::IsThreshold(threshold))
        return fail(PersonResultError::InvalidThreshold);
    const auto parsed = detections_v1::Parse(contract, tensor);
    if (parsed.error != detections_v1::Error::None)
        return fail(PersonResultError::InvalidTensor, parsed.error);
    try {
        std::vector<ObjectInfoV1> objects;
        objects.reserve(parsed.detections.size());
        for (const auto& d : parsed.detections) {
            if (d.score < threshold)
                continue;
            const double cx = d.cx, cy = d.cy, w = d.width, h = d.height;
            const auto x1 = static_cast<float>(std::clamp((cx - w * .5) * 2.4, 0., 1920.));
            const auto x2 = static_cast<float>(std::clamp((cx + w * .5) * 2.4, 0., 1920.));
            const auto y1 = static_cast<float>(std::clamp((cy - h * .5 - 175.) * 2.4, 0., 1080.));
            const auto y2 = static_cast<float>(std::clamp((cy + h * .5 - 175.) * 2.4, 0., 1080.));
            if (!(x2 > x1 && y2 > y1))
                continue;
            ObjectInfoV1 object;
            object.x1 = x1;
            object.y1 = y1;
            object.x2 = x2;
            object.y2 = y2;
            object.infos.push_back({d.score, 0, mouse_contract::kClassLabel});
            objects.push_back(std::move(object));
        }
        return {PersonResultError::None, detections_v1::Error::None, std::move(objects)};
    } catch (const std::bad_alloc&) {
        return fail(PersonResultError::AllocationFailure);
    } catch (const std::length_error&) {
        return fail(PersonResultError::AllocationFailure);
    }
}
}  // namespace cosmo::nn::axera
