#pragma once

#include <memory>

#include "nn/core/blob.h"
#include "nn/core/status.h"

namespace cosmo::nn {

class AbstractFilter;

class PUBLIC Filter {
public:
    explicit Filter(DeviceType);
    ~Filter();

    /**
     * @brief Update filter with blob and region
     *
     * @param blob image blob
     * @param x region x
     * @param y region y
     * @param w region width
     * @param h region height
     * @param average average value(OUT)
     * @return Status
     */
    Status Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average);

    Status SetDeviceId(int id = 0);

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

protected:
    DeviceType device_type;

    std::shared_ptr<AbstractFilter> filter = nullptr;
};

}  // namespace cosmo::nn
