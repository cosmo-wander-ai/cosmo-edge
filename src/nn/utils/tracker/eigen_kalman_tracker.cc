#include "nn/utils/tracker/eigen_kalman_tracker.h"

namespace cosmo::nn {

EigenKalmanTracker::EigenKalmanTracker(Rect2f r, TrackerConfig cfg, unsigned int assigned_id) {
    config     = cfg;
    trajectory = Queue<Rect2f>(config.trajectory_length);
    confidence = -1;
    init_kf(r, assigned_id);
}

void EigenKalmanTracker::Update(Rect2f stateMat, double score) {
    status                 = TRACKING;
    time_since_last_update = 0;
    hits += 1;
    hit_streak += 1;
    confidence = score;
    if (score <= config.thresh[class_id - 1] and score >= config.thresh_low[class_id - 1]) {
        low_thresh_count++;
    } else {
        low_thresh_count = 0;
    }
    // measurement
    measurement(0, 0) = stateMat.x + stateMat.width / 2;
    measurement(1, 0) = stateMat.y + stateMat.height / 2;
    measurement(2, 0) = stateMat.area();
    measurement(3, 0) = stateMat.width / stateMat.height;
    Eigen::MatrixXf p = kf.correct(measurement);

    trajectory.push_back(stateMat);
    last_detect_position  = stateMat;
    last_predict_position = get_rect_xysr(p(0, 0), p(1, 0), p(2, 0), p(3, 0));
    motion_state          = MotionEval();
}

MotionState EigenKalmanTracker::MotionEval() {
    int len;
    trajectory.length() >= config.motion_length ? len = config.motion_length : len = trajectory.length();

    if (len < 2 || config.motion_iou <= 0) {
        return UNCERTAIN;
    }

    float ave_iou = 0;
    if (trajectory.size() >= 2) {
        if (len < 10) {
            int start;
            int end = trajectory.size() - 1;
            trajectory.size() > len ? start = trajectory.size() - len : start = 0;
            ave_iou = GetIOU(trajectory[start], trajectory[end]);
        } else {
            if (trajectory.size() < 10) {
                int start = 0;
                int end   = trajectory.size() - 1;
                ave_iou   = GetIOU(trajectory[start], trajectory[end]);
            } else {
                int start = trajectory.size() - len;
                for (int i = 0; i < 5; i++) {
                    int end = trajectory.size() - 5 + i;
                    ave_iou += GetIOU(trajectory[start + i], trajectory[end]);
                }
                ave_iou = ave_iou / 5;
            }
        }
        if (ave_iou >= config.motion_iou)
            return STILL;
        else
            return MOVING;
    }
    return UNCERTAIN;
}

TrackingBox EigenKalmanTracker::Predict() {
    age += 1;
    // predict
    Eigen::MatrixXf p = kf.predict();
    if (time_since_last_update > 0) {
        hit_streak = 0;
    }

    TrackingBox record;
    record.id             = id;
    record.box            = get_rect_xysr(p(0, 0), p(1, 0), p(2, 0), p(3, 0));
    last_predict_position = record.box;

    time_since_last_update += 1;
    status = LOSS;
    return record;
}

void EigenKalmanTracker::init_kf(Rect2f stateMat, unsigned int assigned_id) {
    const int stateNum     = 7;
    const int measureNum   = 4;
    age                    = 0;
    hits                   = 0;
    hit_streak             = 0;
    motion_state           = UNCERTAIN;  // object is still or not
    id                     = assigned_id;
    status                 = NEW;
    time_since_last_update = 0;
    search_range           = (stateMat.width + stateMat.height) / 2;
    search_range           = search_range > config.max_range ? config.max_range : search_range;
    low_thresh_count       = 0;

    kf          = EigenKalmanFilter(stateNum, measureNum, 0);
    measurement = Eigen::MatrixXf::Zero(measureNum, 1);

    kf.measurementMatrix.setIdentity();
    kf.transitionMatrix << 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1;

    // Q
    kf.processNoiseCov.setIdentity();
    kf.processNoiseCov *= config.process_noise;

    // R
    kf.measurementNoiseCov.setIdentity();
    kf.measurementNoiseCov(0, 0) = config.measurement_noise[0];
    kf.measurementNoiseCov(1, 1) = config.measurement_noise[1];
    kf.measurementNoiseCov(2, 2) = config.measurement_noise[2];
    kf.measurementNoiseCov(3, 3) = config.measurement_noise[3];

    // initialize state vector with bounding box in [cx,cy,s,r] style
    kf.statePost(0, 0) = stateMat.x + stateMat.width / 2;
    kf.statePost(1, 0) = stateMat.y + stateMat.height / 2;
    kf.statePost(2, 0) = stateMat.area();
    kf.statePost(3, 0) = stateMat.width / stateMat.height;

    kf.statePre(0, 0) = stateMat.x + stateMat.width / 2;
    kf.statePre(1, 0) = stateMat.y + stateMat.height / 2;
    kf.statePre(2, 0) = stateMat.area();
    kf.statePre(3, 0) = stateMat.width / stateMat.height;

    measurement(0, 0) = stateMat.x + stateMat.width / 2;
    measurement(1, 0) = stateMat.y + stateMat.height / 2;
    measurement(2, 0) = stateMat.area();
    measurement(3, 0) = stateMat.width / stateMat.height;
    kf.correct(measurement);

    last_detect_position  = stateMat;
    last_predict_position = stateMat;
    trajectory.push_back(last_detect_position);
}

Rect2f EigenKalmanTracker::get_rect_xysr(float cx, float cy, float s, float r) {
    float w = sqrt(s * r);
    float h = s / w;
    float x = (cx - w / 2);
    float y = (cy - h / 2);

    if (x < 0 && cx > 0)
        x = 0;
    if (y < 0 && cy > 0)
        y = 0;

    return Rect2f(x, y, w, h);
}

Rect2f EigenKalmanTracker::GetState() {
    Eigen::MatrixXf s = kf.statePost;
    return get_rect_xysr(s(0, 0), s(1, 0), s(2, 0), s(3, 0));
}

Eigen::MatrixXf EigenKalmanTracker::GetConvariance() {
    return kf.errorCovPre;
}

}  // namespace cosmo::nn