#pragma once

#include <algorithm>
#include <vector>

namespace cosmo::nn {

struct YoloBox {
    // Decoded YOLO coordinates remain center-based through NMS and output.
    float x, y, width, height;
    float confidence;
    int class_id;
};

using YoloBoxVec = std::vector<YoloBox>;

inline std::vector<YoloBox> SophonYoloNms(std::vector<YoloBox>& detections, float iou_threshold) {
    std::vector<YoloBox> result;
    std::sort(detections.begin(), detections.end(),
              [](const YoloBox& a, const YoloBox& b) { return a.confidence > b.confidence; });

    while (!detections.empty()) {
        result.push_back(detections[0]);
        for (auto it = detections.begin() + 1; it != detections.end();) {
            const auto& selected = result.back();
            // Convert each center to its own edges only for the IoU calculation.
            float x1 = std::max(selected.x - selected.width / 2, it->x - it->width / 2);
            float y1 = std::max(selected.y - selected.height / 2, it->y - it->height / 2);
            float x2 = std::min(selected.x + selected.width / 2, it->x + it->width / 2);
            float y2 = std::min(selected.y + selected.height / 2, it->y + it->height / 2);

            float intersection = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
            float area1        = selected.width * selected.height;
            float area2        = it->width * it->height;
            float iou          = intersection / (area1 + area2 - intersection);

            if (iou > iou_threshold) {
                it = detections.erase(it);
            } else {
                ++it;
            }
        }
        detections.erase(detections.begin());
    }

    return result;
}

}  // namespace cosmo::nn
