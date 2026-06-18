#pragma once

#include <memory>

#include "nn/utils/filter.h"
#include "nn/utils/tracker_common.h"

namespace cosmo::nn {

class FireFilter {
public:
    FireFilter(DeviceType type, int device_id, unsigned int assigned_id);

    ~FireFilter();

    Status Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average);

    Status Clear();

    void SetWidth(int w);
    int GetWidth() const;

    void SetHeight(int h);
    int GetHeight() const;

    void SetPosProb(float v);
    float GetPosProb() const;

    void SetNegProb(float v);
    float GetNegProb() const;

    void SetSensiValue(int v);
    int GetSensiValue() const;

    void SetRoi(int v);
    int GetRoi() const;

    int GetElementCount() const;

public:
    unsigned int time_since_last_update = 0;

    int low_thresh_count = 0;

    TrackingStatus status = NEW;

    int class_id     = 0;
    float confidence = 0.0f;

    unsigned int id = 0;

    Rect2f region;

    bool flag = false;

private:
    std::unique_ptr<Filter> filter;
};

}  // namespace cosmo::nn
