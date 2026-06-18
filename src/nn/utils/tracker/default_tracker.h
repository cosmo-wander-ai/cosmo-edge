#pragma once

#include "nn/utils/tracker/abstract_tracker.h"
#include "nn/utils/tracker/eigen_kalman_tracker.h"

namespace cosmo::nn {

struct SecondTracker {
    EigenKalmanTracker tracker;
    int trackers_index;
};

class DefaultTracker : public AbstractTracker {
public:
    DefaultTracker();

    virtual ~DefaultTracker() override;

    virtual Status Update(const std::vector<TrackingBox>& input, std::vector<TrackingBox>& output) override;

private:
    std::vector<EigenKalmanTracker> trackers;
    unsigned int next_id_ = 0;  // per-instance track ID counter (not shared across tasks)
};

}  // namespace cosmo::nn
