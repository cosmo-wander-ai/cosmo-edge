#pragma once

#include "nn/utils/rect.h"
#include "nn/utils/tracker/eigen_kalman_filter.h"
#include "nn/utils/tracker/queue.h"
#include "nn/utils/tracker_common.h"

namespace cosmo::nn {

class EigenKalmanTracker {
public:
    EigenKalmanTracker(Rect2f r, TrackerConfig cfg, unsigned int assigned_id);

    void Update(Rect2f stateMat, double score);

    MotionState MotionEval();

    TrackingBox Predict();

    Rect2f GetState();

    Rect2f get_rect_xysr(float cx, float cy, float s, float r);

    Eigen::MatrixXf GetConvariance();

    unsigned int id                     = 0;
    unsigned int time_since_last_update = 0;
    unsigned int hits                   = 0;
    unsigned int hit_streak             = 0;
    unsigned int age                    = 0;
    unsigned int class_id               = 0;
    float confidence                    = 0.0f;
    double search_range                 = 0.0;
    int low_thresh_count                = 0;
    TrackingStatus status               = NEW;

    Rect2f last_detect_position;
    Rect2f last_predict_position;
    Queue<Rect2f> trajectory;
    MotionState motion_state = UNCERTAIN;  // target motion state

private:
    void init_kf(Rect2f stateMat, unsigned int assigned_id);

    EigenKalmanFilter kf;
    Eigen::MatrixXf measurement;
    TrackerConfig config;
};
}  // namespace cosmo::nn
