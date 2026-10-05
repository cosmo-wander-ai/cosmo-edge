#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "media/VideoFrame.h"
#include "util/Rect.h"

namespace cosmo {
struct AlarmReviewRoi {
    VideoFramePtr frame;
    util::Box requested;
    util::Box actual;
    int sourceWidth{0};
    int sourceHeight{0};
    std::string mode{"invalid"};
};

inline util::Box AlarmReviewCropBox(int width, int height, const util::Box& box) {
    constexpr int64_t padding = 48;
    const auto x0             = std::max<int64_t>(0, static_cast<int64_t>(box.x) - padding);
    const auto y0             = std::max<int64_t>(0, static_cast<int64_t>(box.y) - padding);
    const auto x1             = std::min<int64_t>(width, static_cast<int64_t>(box.x) + box.width + padding);
    const auto y1             = std::min<int64_t>(height, static_cast<int64_t>(box.y) + box.height + padding);
    if (x1 <= x0 || y1 <= y0)
        return {};
    return {static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
}

// Existing Qwen review crop, extracted without initializing any inference service.
// Crop failure retains the existing full-frame fallback and makes it explicit.
AlarmReviewRoi PrepareAlarmReviewRoi(const VideoFramePtr& frame, const util::Box& box);
}  // namespace cosmo
