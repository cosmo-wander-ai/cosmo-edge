#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
namespace benchmark_ax {
struct Detection {
    float x1, y1, x2, y2, score;
    std::size_t anchor;
    int class_id;
};
inline float overlap(const Detection& a, const Detection& b) {
    const float w     = std::max(0.f, std::min(a.x2, b.x2) - std::max(a.x1, b.x1));
    const float h     = std::max(0.f, std::min(a.y2, b.y2) - std::max(a.y1, b.y1));
    const float inter = w * h, aa = std::max(0.f, a.x2 - a.x1) * std::max(0.f, a.y2 - a.y1),
                bb = std::max(0.f, b.x2 - b.x1) * std::max(0.f, b.y2 - b.y1);
    return aa + bb > inter ? inter / (aa + bb - inter) : 0.f;
}
inline std::vector<Detection> person(const float* boxes, std::size_t boxes_size, const float* scores,
                                     std::size_t scores_size, int width, int height, float confidence = .25f,
                                     float nms_iou = .7f, std::size_t topk = 1000) {
    constexpr std::size_t n = 8400;
    if (!boxes || !scores || boxes_size != 4 * n || scores_size != 80 * n || width <= 0 || height <= 0)
        throw std::invalid_argument("benchmark detector contract");
    std::vector<Detection> candidates;
    auto append = [&](std::size_t i, float s, int cls) {
        const float cx = boxes[i], cy = boxes[n + i], w = boxes[2 * n + i], h = boxes[3 * n + i];
        if (s < confidence || !std::isfinite(s) || !std::isfinite(cx) || !std::isfinite(cy) ||
            !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0)
            return;
        candidates.push_back({cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, s, i, cls});
    };
#if defined(__aarch64__)
    constexpr std::size_t vectors = 4;
    constexpr std::size_t lanes   = 4;
    constexpr std::size_t tile    = vectors * lanes;
    static_assert(n % tile == 0);
    for (std::size_t i = 0; i < n; i += tile) {
        std::array<float32x4_t, vectors> maxima;
        std::array<int32x4_t, vectors> classes;
        for (std::size_t v = 0; v < vectors; ++v) {
            maxima[v]  = vld1q_f32(scores + i + v * lanes);
            classes[v] = vdupq_n_s32(0);
        }
        // Preserve strict comparisons and increasing class and anchor order.
        for (int c = 1; c < 80; ++c) {
            const auto classId = vdupq_n_s32(c);
            const auto* row    = scores + c * n + i;
            for (std::size_t v = 0; v < vectors; ++v) {
                const auto values = vld1q_f32(row + v * lanes);
                const auto better = vcgtq_f32(values, maxima[v]);
                maxima[v]         = vbslq_f32(better, values, maxima[v]);
                classes[v]        = vbslq_s32(better, classId, classes[v]);
            }
        }
        std::array<float, tile> bestScores;
        std::array<std::int32_t, tile> bestClasses;
        for (std::size_t v = 0; v < vectors; ++v) {
            vst1q_f32(bestScores.data() + v * lanes, maxima[v]);
            vst1q_s32(bestClasses.data() + v * lanes, classes[v]);
        }
        for (std::size_t k = 0; k < tile; ++k)
            append(i + k, bestScores[k], bestClasses[k]);
    }
#else
    for (std::size_t i = 0; i < n; ++i) {
        int cls = 0;
        float s = scores[i];
        for (int c = 1; c < 80; ++c)
            if (scores[c * n + i] > s) {
                s   = scores[c * n + i];
                cls = c;
            }
        append(i, s, cls);
    }
#endif
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](auto& a, auto& b) { return a.score > b.score; });
    std::array<std::vector<Detection>, 80> classes;
    std::size_t retained = 0;
    for (const auto& b : candidates) {
        bool suppressed = false;
        auto& bucket    = classes[static_cast<std::size_t>(b.class_id)];
        for (const auto& a : bucket)
            if (overlap(a, b) >= nms_iou) {
                suppressed = true;
                break;
            }
        if (!suppressed) {
            bucket.push_back(b);
            if (++retained >= topk)
                break;
        }
    }
    std::vector<Detection> kept = std::move(classes[0]);
    const float scale           = std::min(640.f / width, 640.f / height);
    const int resized_w = std::max(1, int(width * scale)), resized_h = std::max(1, int(height * scale));
    const float sx = float(resized_w) / width, sy = float(resized_h) / height;
    const float pad_x = std::floor((640 - resized_w) / 2.f), pad_y = std::floor((640 - resized_h) / 2.f);
    for (auto& b : kept) {
        b.x1 = std::clamp((b.x1 - pad_x) / sx, 0.f, float(width));
        b.x2 = std::clamp((b.x2 - pad_x) / sx, 0.f, float(width));
        b.y1 = std::clamp((b.y1 - pad_y) / sy, 0.f, float(height));
        b.y2 = std::clamp((b.y2 - pad_y) / sy, 0.f, float(height));
    }
    return kept;
}
inline int helmet(const float* scores, std::size_t n) {
    if (!scores || n != 2 || !std::isfinite(scores[0]) || !std::isfinite(scores[1]))
        throw std::invalid_argument("benchmark helmet contract");
    const int cls = scores[1] > scores[0] ? 1 : 0;
    return scores[cls] >= .5f ? cls : -1;
}
}  // namespace benchmark_ax
