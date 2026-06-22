#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief CPU sequence node.
 *
 * Stitches multiple cropped images into a single grid, then normalizes
 * to float32 NCHW format. Used for temporal/spatial sequence models.
 * Pure CPU implementation of SophonSequenceNode.
 */
class CpuSequenceNode : public Node {
public:
    CpuSequenceNode();
    ~CpuSequenceNode() override = default;

    void LoadParam(Op* op) override;
    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& params,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status PrepareRect(std::shared_ptr<Blob> host_rect, int image_w, int image_h);

    static void BilinearResize(const uint8_t* src, int src_w, int src_h, int channels, uint8_t* dst,
                               int dst_w, int dst_h);

    float scale    = 1.0f;
    bool is_bgr    = true;
    int dst_batch  = 0;
    int dst_depth  = 0;
    int dst_width  = 0;
    int dst_height = 0;

    std::vector<int32_t> calculated_rects;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
