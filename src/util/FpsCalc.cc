// FpsCalc — Frame rate calculator based on a sliding interval.

#include "util/FpsCalc.h"

namespace cosmo::util {

void FpsCalc::Reset() {
    start_time_  = Clock::now();
    frame_index_ = 0;
    fps_         = 0.0f;
}

float FpsCalc::Fps() {
    ++frame_index_;

    if (frame_index_ % interval_ == 0) {
        auto now         = Clock::now();
        float duration_s = std::chrono::duration<float>(now - start_time_).count();

        if (duration_s > 0.0f) {
            fps_ = static_cast<float>(interval_) / duration_s;
        } else if (fps_ == 0.0f) {
            fps_ = 25.0f;  // Default realistic fallback
        }

        start_time_ = now;
    } else if (frame_index_ < interval_) {
        // Calculate progressively from startup until first full interval.
        auto now         = Clock::now();
        float duration_s = std::chrono::duration<float>(now - start_time_).count();

        if (duration_s > 0.0f) {
            fps_ = static_cast<float>(frame_index_) / duration_s;
        } else if (fps_ == 0.0f) {
            fps_ = 25.0f;
        }
    }

    return fps_;
}

std::pair<size_t, float> FpsCalc::FpsWithFrame() {
    auto fps = Fps();
    return {frame_index_, fps};
}

}  // namespace cosmo::util
