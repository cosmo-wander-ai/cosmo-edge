#pragma once
// Isolated experiment: reproduce the existing person-only NMS32 graph on CPU.
// Input is the already available, contiguous FP32 [1,84,8400] detector output.
// Return 1 to run the existing NMS128 overflow fallback, or 2 to run the
// original NMS32 path for non-finite inputs; output is not usable in either
// case. This is not a general 80-class postprocessor.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__GNUC__) && !defined(__clang__)
#define PERSON_NO_FMA __attribute__((optimize("fp-contract=off")))
#else
#define PERSON_NO_FMA
#endif

namespace {
constexpr int kPoints  = 8400;
constexpr int kClasses = 80;
constexpr int kKeep    = 32;
struct Candidate {
    int i;
    float score;
};

PERSON_NO_FMA float IoU(const float* a, const float* b) {
    const float left         = std::max(a[0] - a[2] * 0.5f, b[0] - b[2] * 0.5f);
    const float right        = std::min(a[0] + a[2] * 0.5f, b[0] + b[2] * 0.5f);
    const float top          = std::max(a[1] - a[3] * 0.5f, b[1] - b[3] * 0.5f);
    const float bottom       = std::min(a[1] + a[3] * 0.5f, b[1] + b[3] * 0.5f);
    const float intersection = std::max(0.0f, right - left) * std::max(0.0f, bottom - top);
    const float denominator  = a[2] * a[3] + b[2] * b[3] - intersection;
    if (denominator == 0.0f)
        return intersection > 0.0f ? 1.0f : 0.0f;
    return intersection / denominator;
}
}  // namespace

// rows [32,6] is sorted and keep [32] is 0/1. Caller owns all buffers.
inline PERSON_NO_FMA int person_postprocess32(const float* input, float* rows, float* keep) {
    if (!input || !rows || !keep)
        return -1;
    std::memset(rows, 0, sizeof(float) * kKeep * 6);
    std::memset(keep, 0, sizeof(float) * kKeep);
    std::vector<Candidate> candidates;
    candidates.reserve(kKeep + 1);
    for (int i = 0; i < kPoints; ++i) {
        const float score = input[4 * kPoints + i];
        // Non-finite inputs require the original NPU path; avoid inventing
        // an ordering for ONNX TopK and comparisons in that case.
        if (!std::isfinite(score))
            return 2;
        if (score >= 0.25f) {
            if (candidates.size() == kKeep)
                return 1;
            candidates.push_back({i, score});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](Candidate a, Candidate b) {
        return a.score > b.score || (a.score == b.score && a.i < b.i);
    });
    for (size_t j = 0; j < candidates.size(); ++j) {
        const auto c = candidates[j];
        float* row   = rows + j * 6;
        for (int k = 0; k < 4; ++k) {
            row[k] = input[k * kPoints + c.i];
            if (!std::isfinite(row[k]))
                return 2;
        }
        // ArgMax select_last_index=0: person wins exact ties.
        for (int k = 1; k < kClasses; ++k) {
            const float other = input[(4 + k) * kPoints + c.i];
            if (!std::isfinite(other))
                return 2;
            if (other > c.score)
                goto next_candidate;
        }
        row[4]  = c.score;
        row[5]  = 0;
        keep[j] = 1;
    next_candidate:;
    }
    for (int i = 0; i < kKeep; ++i) {
        if (!keep[i])
            continue;
        for (int j = i + 1; j < kKeep; ++j) {
            if (keep[j] && IoU(rows + i * 6, rows + j * 6) >= 0.7f)
                keep[j] = 0;
        }
    }
    return 0;
}

#undef PERSON_NO_FMA
