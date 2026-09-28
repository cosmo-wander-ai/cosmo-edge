#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cosmo::nn {

struct Yolo26RawDetection {
    float cx         = 0.0f;
    float cy         = 0.0f;
    float width      = 0.0f;
    float height     = 0.0f;
    float confidence = 0.0f;
    int class_id     = -1;
};

// Decode a channel-major YOLO26 head [4 + classes, N]: pixel center xywh
// followed by per-class probabilities. NMS is class-aware and suppresses only
// when IoU is strictly greater than the limit.
inline std::vector<Yolo26RawDetection> DecodeYolo26RawHead(const float* channel_major,
                                                           std::size_t candidate_count,
                                                           std::size_t channel_count,
                                                           float confidence_threshold, float iou_threshold,
                                                           std::size_t max_detections) {
    std::vector<Yolo26RawDetection> candidates;
    if (!channel_major || channel_count <= 4 || candidate_count == 0 || max_detections == 0 ||
        !std::isfinite(confidence_threshold) || confidence_threshold < 0.f || confidence_threshold > 1.f ||
        !std::isfinite(iou_threshold) || iou_threshold < 0.f || iou_threshold > 1.f)
        return candidates;

    const std::size_t class_count = channel_count - 4;
    candidates.reserve(candidate_count);
    for (std::size_t i = 0; i < candidate_count; ++i) {
        const float cx = channel_major[0 * candidate_count + i];
        const float cy = channel_major[1 * candidate_count + i];
        const float w  = channel_major[2 * candidate_count + i];
        const float h  = channel_major[3 * candidate_count + i];
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(w) || !std::isfinite(h) || w <= 0.0f ||
            h <= 0.0f)
            continue;

        float best_score = -1.0f;
        int best_class   = -1;
        for (std::size_t c = 0; c < class_count; ++c) {
            const float score = channel_major[(4 + c) * candidate_count + i];
            if (std::isfinite(score) && score >= 0.f && score <= 1.f && score > best_score) {
                best_score = score;
                best_class = static_cast<int>(c);
            }
        }
        if (best_class < 0 || best_score <= confidence_threshold)
            continue;
        candidates.push_back({cx, cy, w, h, best_score, best_class});
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& lhs, const auto& rhs) { return lhs.confidence > rhs.confidence; });

    std::vector<Yolo26RawDetection> kept;
    kept.reserve(std::min(max_detections, candidates.size()));
    for (const auto& candidate : candidates) {
        bool suppressed             = false;
        const double left           = candidate.cx - candidate.width * 0.5;
        const double top            = candidate.cy - candidate.height * 0.5;
        const double right          = candidate.cx + candidate.width * 0.5;
        const double bottom         = candidate.cy + candidate.height * 0.5;
        const double candidate_area = static_cast<double>(candidate.width) * candidate.height;
        for (const auto& selected : kept) {
            if (selected.class_id != candidate.class_id)
                continue;
            const double selected_left   = selected.cx - selected.width * 0.5;
            const double selected_top    = selected.cy - selected.height * 0.5;
            const double selected_right  = selected.cx + selected.width * 0.5;
            const double selected_bottom = selected.cy + selected.height * 0.5;
            const double intersection_width =
                std::max(0.0, std::min(right, selected_right) - std::max(left, selected_left));
            const double intersection_height =
                std::max(0.0, std::min(bottom, selected_bottom) - std::max(top, selected_top));
            const double intersection  = intersection_width * intersection_height;
            const double selected_area = static_cast<double>(selected.width) * selected.height;
            const double union_area    = candidate_area + selected_area - intersection;
            const double iou           = union_area > 0.0f ? intersection / union_area : 0.0f;
            if (iou > iou_threshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(candidate);
            if (kept.size() == max_detections)
                break;
        }
    }
    return kept;
}

}  // namespace cosmo::nn
