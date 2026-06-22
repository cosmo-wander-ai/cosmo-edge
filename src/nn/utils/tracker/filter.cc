#include "nn/utils/filter.h"

#include "nn/utils/tracker/abstract_filter.h"

namespace cosmo::nn {

#define CHECK_FILTER                                                                                         \
    if (!filter) {                                                                                           \
        return Status(COSMO_NN_ERR_NULL_PARAM, "Abstract filter is null, check device type");                \
    }

Filter::Filter(DeviceType type) : device_type(type) {
    filter = FilterManager::Instance()->CreateFilter(device_type);
    if (!filter) {
        throw std::bad_alloc();
    }
}

Filter::~Filter() {
    if (filter) {
        filter.reset();
    }
}

Status Filter::Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average) {
    auto blob_device_type = blob->GetBlobDesc().device_type;
    if (blob_device_type != device_type) {
        return Status(COSMO_NN_ERR_PARAM, "Incompatible blob device type");
    }

    CHECK_FILTER;
    return filter->Update(blob, x, y, w, h, average);
}

Status Filter::Clear() {
    CHECK_FILTER;
    return filter->Clear();
}

Status Filter::SetDeviceId(int id) {
    CHECK_FILTER;
    return filter->SetDeviceId(id);
}

int Filter::GetWidth() const {
    if (filter) {
        return filter->GetWidth();
    }
    return 0;
}

void Filter::SetWidth(int v) {
    if (filter) {
        filter->SetWidth(v);
    }
}

int Filter::GetHeight() const {
    if (filter) {
        return filter->GetHeight();
    }
    return 0;
}

void Filter::SetHeight(int v) {
    if (filter) {
        filter->SetHeight(v);
    }
}

void Filter::SetPosProb(float v) {
    if (filter) {
        filter->SetPosProb(v);
    }
}

float Filter::GetPosProb() const {
    if (filter) {
        return filter->GetPosProb();
    }
    return 0;
}

void Filter::SetNegProb(float v) {
    if (filter) {
        filter->SetNegProb(v);
    }
}

float Filter::GetNegProb() const {
    if (filter) {
        return filter->GetNegProb();
    }
    return 0;
}

void Filter::SetSensiValue(int v) {
    if (filter) {
        filter->SetSensiValue(v);
    }
}

int Filter::GetSensiValue() const {
    if (filter) {
        return filter->GetSensiValue();
    }
    return 0;
}

void Filter::SetRoi(int v) {
    if (filter) {
        filter->SetRoi(v);
    }
}

int Filter::GetRoi() const {
    if (filter) {
        return filter->GetRoi();
    }
    return 0;
}

int Filter::GetElementCount() const {
    if (filter) {
        return filter->GetElementCount();
    }
    return 0;
}

}  // namespace cosmo::nn