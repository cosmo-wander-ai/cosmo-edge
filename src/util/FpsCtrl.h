// Frame rate control utility.
// Used to limit processing frequency based on target fps.
#pragma once

#include <chrono>

namespace cosmo::util {

class FpsCtrl {
public:
    explicit FpsCtrl(float real_fps = 0.0f, float ctrl_fps = 0.0f);
    ~FpsCtrl() = default;

    void ChangeFps(float real_fps, float ctrl_fps);

    [[nodiscard]] bool IsFilter(size_t frame_index) const;
    [[nodiscard]] bool IsFilter(size_t frame_index, float ctrl_fps,
                                std::chrono::steady_clock::time_point frame_time_point);

    [[nodiscard]] double GetRealFps() const;
    void SetRealFps(float real_fps);

private:
    static constexpr int64_t kFpsMultiCount = 100;

    double real_fps_{0.0};
    double ctrl_fps_{0.0};
    int64_t i_real_fps_{0};
    int64_t i_ctrl_fps_{0};
    std::chrono::steady_clock::time_point last_distribute_time_point_;
    bool first_frame_processed_{false};
};

}  // namespace cosmo::util
