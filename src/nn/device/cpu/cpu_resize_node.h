#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief CPU resize node — bilinear interpolation with optional letterbox padding.
 *
 * Supports the same gravity modes as SophonResizeNode:
 *   0: stretch to target size
 *   1: keep aspect ratio, center padding
 *   2: keep aspect ratio, top-left align
 */
class CpuResizeNode : public Node {
public:
    CpuResizeNode();
    ~CpuResizeNode() override = default;

    void LoadParam(Op* op) override;
    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status ResizeSingle(std::shared_ptr<Blob>& bottom, std::shared_ptr<Blob>& top);

    static void BilinearResize(const uint8_t* src, int src_w, int src_h, int src_channels, uint8_t* dst,
                               int dst_w, int dst_h);

    int out_h_   = 0;
    int out_w_   = 0;
    int gravity_ = 0;
    uint8_t pad_ = 114;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
