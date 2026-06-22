// FpsCtrl — Frame rate control utility.

#include "util/FpsCtrl.h"

#include <cmath>

#include "util/Log.h"
#include "util/VideoInfo.h"

namespace cosmo::util {

FpsCtrl::FpsCtrl(float real_fps, float ctrl_fps) {
    ChangeFps(real_fps, ctrl_fps);
}

void FpsCtrl::ChangeFps(float real_fps, float ctrl_fps) {
    real_fps_ = real_fps;
    ctrl_fps_ = ctrl_fps;

    i_real_fps_ = static_cast<int64_t>(std::round(real_fps_ * kFpsMultiCount));
    i_ctrl_fps_ = static_cast<int64_t>(std::round(ctrl_fps_ * kFpsMultiCount));
}

bool FpsCtrl::IsFilter(size_t frame_index) const {
    if (i_real_fps_ <= 0) {
        return true;  // Filter directly if real fps is less than or equal to 0
    }

    if (i_ctrl_fps_ <= 0) {
        return false;  // Do not control if target fps is less than or equal to 0
    }

    if (i_real_fps_ <= i_ctrl_fps_) {
        return false;  // Do not control if real fps is less than target fps
    }

    if ((static_cast<int64_t>(frame_index) * i_ctrl_fps_ % i_real_fps_) < i_ctrl_fps_) {
        return false;
    }

    return true;
}

bool FpsCtrl::IsFilter(size_t frame_index, float ctrl_fps,
                       std::chrono::steady_clock::time_point frame_time_point) {
    int64_t i_ctrl_fps = static_cast<int64_t>(std::round(ctrl_fps * kFpsMultiCount));

    if (!first_frame_processed_) {
        last_distribute_time_point_ = frame_time_point;
        first_frame_processed_      = true;
    }

    if ((i_ctrl_fps <= 0) || (static_cast<double>(i_ctrl_fps) >= media::kFpsCtrlMaxFps)) {
        last_distribute_time_point_ = frame_time_point;
        return false;  // Do not control if target fps is out of bounds
    }

    if (i_real_fps_ <= i_ctrl_fps) {
        last_distribute_time_point_ = frame_time_point;
        return false;  // Do not control if real fps is less than target fps
    }

    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(frame_time_point - last_distribute_time_point_)
            .count();
    int64_t ctrl_duration = static_cast<int64_t>(1000.0f / ctrl_fps);
    if (duration_ms > ctrl_duration) {  // Prevent dropping frames
        last_distribute_time_point_ = frame_time_point;
        return false;
    }

    if (i_real_fps_ <= 0) {
        return true;  // Filter directly if real fps is less than or equal to 0
    }

    if ((static_cast<int64_t>(frame_index) * i_ctrl_fps % i_real_fps_) < i_ctrl_fps) {
        last_distribute_time_point_ = frame_time_point;
        return false;
    }

    return true;
}

double FpsCtrl::GetRealFps() const {
    return real_fps_;
}

void FpsCtrl::SetRealFps(float real_fps) {
    real_fps_   = real_fps;
    i_real_fps_ = static_cast<int64_t>(std::round(real_fps_ * kFpsMultiCount));
}

}  // namespace cosmo::util
