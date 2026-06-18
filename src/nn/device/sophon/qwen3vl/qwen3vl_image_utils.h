#pragma once

#include <utility>
#include <vector>

#include "nn/device/sophon/qwen3vl/qwen3vl_config.h"

namespace cosmo::nn {
namespace qwen3vl {

    // Preprocess from BGR packed (HWC) identical to demo process_image, output pixel_values and set
    // config.grid_thw
    bool process_image_from_mat(const unsigned char* bgr_data, int width, int height, int stride_bytes,
                                Config& config, std::vector<float>& pixel_values);

}  // namespace qwen3vl
}  // namespace cosmo::nn
