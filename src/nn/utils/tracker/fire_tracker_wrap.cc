#include "nn/utils/tracker/fire_tracker.h"
#include "nn/utils/tracker_wrap.h"

namespace cosmo::nn {

FireTrackerWrap::FireTrackerWrap(DeviceType type, int device_id) {
    impl = std::make_shared<FireTracker>(type, device_id);
}

FireTrackerWrap::~FireTrackerWrap() {
    impl.reset();
}

Status FireTrackerWrap::GetRegion(Rect2f& region) {
    if (impl)
        return impl->GetRegion(region);

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}

Status FireTrackerWrap::SetRegion(Rect2f& region) {
    if (impl)
        return impl->SetRegion(region);

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}

Status FireTrackerWrap::GetTrackerConfig(TrackerConfig& c) {
    if (impl)
        return impl->GetTrackerConfig(c);

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}

Status FireTrackerWrap::SetTrackerConfig(TrackerConfig& c) {
    if (impl)
        return impl->SetTrackerConfig(c);

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}

Status FireTrackerWrap::SetPosProb(float v) {
    if (impl) {
        impl->SetPosProb(v);
        return COSMO_NN_OK;
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}

float FireTrackerWrap::GetPosProb() {
    if (impl)
        return impl->GetPosProb();

    return 0;
};

Status FireTrackerWrap::SetNegProb(float v) {
    if (impl) {
        impl->SetNegProb(v);
        return COSMO_NN_OK;
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}

float FireTrackerWrap::GetNegProb() {
    if (impl)
        return impl->GetNegProb();

    return 0;
}

Status FireTrackerWrap::SetSensiValue(int v) {
    if (impl) {
        impl->SetSensiValue(v);
        return COSMO_NN_OK;
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}
int FireTrackerWrap::GetSensiValue() {
    if (impl)
        return impl->GetSensiValue();

    return 0;
}

Status FireTrackerWrap::Trace(const std::vector<TrackingBox>& input) {
    if (impl) {
        std::vector<TrackingBox> output;
        return impl->Update(input, output);
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "FireTracker impl is nullptr");
}

Status FireTrackerWrap::Filter(std::shared_ptr<Blob> blob) {
    if (impl)
        return impl->Filter(blob);

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "Tracker impl is nullptr");
}

Status FireTrackerWrap::GetResult(std::vector<TrackingBox>& output) {
    if (impl) {
        output.clear();
        return impl->GetResult(output);
    }

    return Status(COSMO_NN_ERR_TRACKER_CREATION, "Tracker impl is nullptr");
}

}  // namespace cosmo::nn