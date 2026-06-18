#pragma once

#include "nn/node/node.h"

// #include "bmcv_api.h"
// #include "bmcv_api_ext.h"
// #include "bmlib_runtime.h"
// #include "bmruntime_cpp.h"

namespace cosmo::nn {

/**
 * @brief SophonNormalizeNode is a node for normalize data.
 * @brief swap r and b channel if need. y = (x - mean) * scale and convert from NHWC to NCHW
 */
class SophonNormalizeNode : public Node {
public:
    SophonNormalizeNode();

    virtual ~SophonNormalizeNode();

    virtual void LoadParam(Op* op) override;

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual bool NeedBottomShapesInfered() override;
    virtual Status InferTopShapesWithBottoms(std::vector<DimsVector> dims,
                                             std::vector<DataType> types) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    bool NeedSwapRB(ImageFormat fmt);

private:
    std::vector<float> mean{};
    std::vector<float> std{};
    float scale = 0;
    bool is_bgr = true;
    float bm_model_input_scale;
    float alpha;
    float beta;
    // bmcv_convert_to_attr converto_attr;
};

}  // namespace cosmo::nn
