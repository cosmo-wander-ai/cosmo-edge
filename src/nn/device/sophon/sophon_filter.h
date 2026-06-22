#pragma once

#include <memory>
#include <vector>

#include "bmcv_api.h"
#include "bmlib_runtime.h"
#include "nn/utils/tracker/abstract_filter.h"

namespace cosmo::nn {

class SophonFilter : public AbstractFilter {
public:
    explicit SophonFilter(DeviceType);
    virtual ~SophonFilter() override;

    virtual Status Update(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average) override;

    virtual Status SetDeviceId(int id = 0) override;

    virtual Status Clear() override;

private:
    Status ExtractRChannelAndResize(std::shared_ptr<Blob> blob, int x, int y, int w, int h);

    Status StreamOrderedUpdate(std::shared_ptr<Blob> blob, int x, int y, int w, int h, float* average);

    void ClearBMImage(bm_image* image);

    void FreeDeviceMemory(bm_device_mem_t* mem);

    Status AttachImage(bm_image* image, int width, int height, bm_image_format_ext format,
                       bm_device_mem_t* mem);

    Status MallocDeviceMemory(std::unique_ptr<bm_device_mem_t>& mem, int size);

    Status AbsDiffBinary(float threshold, float val_low, float val_high);

    void Dilate(float* src, float* dst, int w, int h, int kernel_size = 3);

    void Blur(float* src, float* dst, int w, int h, int kernel_size = 3);

private:
    bool memory_allocated = false;
    bool first_frame      = true;

    int current_device_id = 0;

    bm_handle_t bm_handle;

    std::unique_ptr<bm_device_mem_t> resized_image_b;
    std::unique_ptr<bm_device_mem_t> resized_image_g;
    std::unique_ptr<bm_device_mem_t> resized_image_r;

    std::vector<uint8_t> host_current_image;
    std::vector<uint8_t> host_previous_image;
    std::vector<float> host_abs_diff_binary;
    std::vector<float> host_dilate;
    std::vector<float> host_blur;
    std::vector<float> host_prob;
    std::vector<float> host_prob_thresh;
};

}  // namespace cosmo::nn
