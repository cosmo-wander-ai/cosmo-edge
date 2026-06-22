#include "nn/utils/tracker/abstract_tracker.h"

namespace cosmo::nn {

AbstractTracker::AbstractTracker() {}

AbstractTracker::~AbstractTracker() {}

Status AbstractTracker::SetRegion(Rect2f& region_) {
    track_region = region_;
    return COSMO_NN_OK;
}

Status AbstractTracker::GetRegion(Rect2f& region_) {
    region_ = track_region;
    return COSMO_NN_OK;
}

Status AbstractTracker::SetTrackerConfig(TrackerConfig& c) {
    config = c;
    return COSMO_NN_OK;
}

Status AbstractTracker::GetTrackerConfig(TrackerConfig& c) {
    c = config;
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn