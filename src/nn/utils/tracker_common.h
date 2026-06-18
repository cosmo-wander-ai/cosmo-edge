#pragma once

#include <vector>

#include "nn/core/macros.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

enum TrackingStatus { NEW, TRACKING, LOSS };

enum MotionState { UNCERTAIN, MOVING, STILL };

struct PUBLIC TrackingBox {
    int id                = 0;
    int class_id          = 0;
    float confidence      = 0.0f;
    TrackingStatus status = NEW;
    Rect2f box;

    // Queue<Rect2f> trajectory;
    MotionState motion_state = UNCERTAIN;  // target motion state, motion_iou=0 don't evaluate state
};

struct PUBLIC TrackerConfig {
    int max_age                           = 5;
    int min_hits                          = 1;
    int max_range                         = 300;
    int trajectory_length                 = 50;
    double dynamic_match                  = 0.8;
    double process_noise                  = 1;
    std::vector<double> measurement_noise = {0.1, 0.1, 10, 10};
    bool classid_same                     = true;
    int vertex_out_of_region              = 2;
    float motion_iou  = 80;  // motion_iou >0 and trajectory_length >=2, evaluate motion state
    int motion_length = 5;   // length of history frame to evaluate motion state
    std::vector<float> thresh{};
    std::vector<float> thresh_low;
    int thresh_low_timeout = 15;
};

}  // namespace cosmo::nn
