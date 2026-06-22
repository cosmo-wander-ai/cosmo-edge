#include "nn/utils/tracker_wrap.h"

#include "nn/utils/tracker/default_tracker.h"

namespace cosmo::nn {

TrackerWrap::TrackerWrap() {
    impl = std::make_shared<DefaultTracker>();
}

TrackerWrap::~TrackerWrap() {
    impl.reset();
}

Status TrackerWrap::GetRegion(Rect2f& region) {
    if (impl) {
        return impl->GetRegion(region);
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "Tracker impl is nullptr");
}

Status TrackerWrap::SetRegion(Rect2f& region) {
    if (impl) {
        return impl->SetRegion(region);
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "Tracker impl is nullptr");
}

Status TrackerWrap::GetTrackerConfig(TrackerConfig& c) {
    if (impl) {
        return impl->GetTrackerConfig(c);
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "Tracker impl is nullptr");
}

Status TrackerWrap::SetTrackerConfig(TrackerConfig& c) {
    if (impl) {
        return impl->SetTrackerConfig(c);
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "Tracker impl is nullptr");
}

Status TrackerWrap::Trace(const std::vector<TrackingBox>& input, std::vector<TrackingBox>& output) {
    if (impl) {
        return impl->Update(input, output);
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "Tracker impl is nullptr");
}

}  // namespace cosmo::nn