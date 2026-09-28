#pragma once

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <cstddef>
#include <string>
#include <vector>

namespace cosmo::nn {

struct RknnYolo26Head {
    const float* data{nullptr};
    size_t element_count{0};
    std::vector<int> shape;
};

struct RknnYolo26Layout {
    int class_count{0};
    int point_count{0};
    std::vector<int> logical_shape;
};

// One-to-one YOLO26 Detect heads: (box[4], class[nc]) at three ordered scales.
bool DetectRknnYolo26Layout(const std::vector<std::vector<int>>& shapes, RknnYolo26Layout& layout,
                            std::string& error);

// Recreate the exported end-to-end [1, 300, 6] xyxy/score/class tensor.
bool ReconstructRknnYolo26(const std::vector<RknnYolo26Head>& heads, int input_height, int input_width,
                           float* output, size_t output_count, std::string& error);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
