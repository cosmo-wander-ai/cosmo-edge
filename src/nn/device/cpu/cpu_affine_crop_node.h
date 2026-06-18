#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief CPU affine-crop node.
 *
 * Performs affine-transform crop based on facial landmarks (5-point).
 * Computes a 2x3 affine matrix from landmarks, then applies inverse
 * warp with bilinear interpolation entirely on CPU host memory.
 */
class CpuAffineCropNode : public Node {
public:
    CpuAffineCropNode();
    ~CpuAffineCropNode() override = default;

    void LoadParam(Op* op) override;
    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& params,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status CalculateMatrix(float* landmark, std::vector<float>& matrix);

    float norm_ratio = 0.0f;
    int norm_mode    = 0;
    int dst_width    = 0;
    int dst_height   = 0;
    std::vector<int> center_index;
    static constexpr int kLandmarkDataLen = 10;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
