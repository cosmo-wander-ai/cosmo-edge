#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief CPU normalize node — converts NHWC uint8 image to NCHW float blob.
 *
 * Applies: output[c] = (input[c] - mean[c]) * scale[c]
 * Also swaps R/B channel if needed.
 */
class CpuNormalizeNode : public Node {
public:
    CpuNormalizeNode();
    ~CpuNormalizeNode() override = default;

    void LoadParam(Op* op) override;
    DeviceType GetTopBlobDeviceType() override;
    bool NeedBottomShapesInfered() override;
    Status InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    bool NeedSwapRB(ImageFormat fmt);

    std::vector<float> mean_{};
    std::vector<float> scale_{};  // pre-computed: 1/std or scale
    bool is_bgr_ = true;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
