#include "nn/utils/tracker/fire_filter.h"

namespace cosmo::nn {

FireFilter::FireFilter(DeviceType type, int device_id, unsigned int assigned_id) {
    time_since_last_update = 0;
    low_thresh_count       = 0;
    id                     = assigned_id;
    status                 = TrackingStatus::NEW;
    flag                   = false;

    filter = std::make_unique<Filter>(type);
    filter->SetDeviceId(device_id);
    filter->SetPosProb(15);
    filter->SetNegProb(13);
    filter->SetSensiValue(128);
}

FireFilter::~FireFilter() {}

int FireFilter::GetWidth() const {
    if (filter) {
        return filter->GetWidth();
    }
    return 0;
}

void FireFilter::SetWidth(int v) {
    if (filter) {
        filter->SetWidth(v);
    }
}

int FireFilter::GetHeight() const {
    if (filter) {
        return filter->GetHeight();
    }
    return 0;
}

void FireFilter::SetHeight(int v) {
    if (filter) {
        filter->SetHeight(v);
    }
}

void FireFilter::SetPosProb(float v) {
    if (filter) {
        filter->SetPosProb(v);
    }
}

float FireFilter::GetPosProb() const {
    if (filter) {
        return filter->GetPosProb();
    }
    return 0;
}

void FireFilter::SetNegProb(float v) {
    if (filter) {
        filter->SetNegProb(v);
    }
}

float FireFilter::GetNegProb() const {
    if (filter) {
        return filter->GetNegProb();
    }
    return 0;
}

void FireFilter::SetSensiValue(int v) {
    if (filter) {
        filter->SetSensiValue(v);
    }
}

int FireFilter::GetSensiValue() const {
    if (filter) {
        return filter->GetSensiValue();
    }
    return 0;
}

void FireFilter::SetRoi(int v) {
    if (filter) {
        filter->SetRoi(v);
    }
}

int FireFilter::GetRoi() const {
    if (filter) {
        return filter->GetRoi();
    }
    return 0;
}

int FireFilter::GetElementCount() const {
    if (filter) {
        return filter->GetElementCount();
    }
    return 0;
}

Status FireFilter::Clear() {
    if (filter)
        return filter->Clear();

    return Status(COSMO_NN_ERR_NULL_PARAM, "Filter is nullptr, check device type");
}

Status FireFilter::Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average) {
    time_since_last_update++;
    Status status = COSMO_NN_OK;

    if (average) {
        status = filter->Update(blob, x, y, w, h, average);
        flag   = (*average >= 0.05 * filter->GetSensiValue());
    } else {
        float val;
        status = filter->Update(blob, x, y, w, h, &val);
        flag   = (val >= 0.05 * filter->GetSensiValue());
    }

    return status;
}

}  // namespace cosmo::nn