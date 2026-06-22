#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief CPU crop-resize node.
 *
 * Takes an image blob and a rect blob, crops ROIs from the image,
 * then resizes each crop to the target size.
 * Supports the same gravity modes and square-crop options as SophonCropResizeNode.
 */
class CpuCropResizeNode : public Node {
public:
    CpuCropResizeNode();
    ~CpuCropResizeNode() override = default;

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
    Status PrepareRect(std::shared_ptr<Blob> host_rect, int image_w, int image_h);

    static void BilinearResize(const uint8_t* src, int src_w, int src_h, int channels, uint8_t* dst,
                               int dst_w, int dst_h);

    std::string type;
    std::vector<float> h_top_crop;
    std::vector<float> h_bottom_crop;
    std::vector<float> w_left_crop;
    std::vector<float> w_right_crop;
    bool square     = false;
    int square_mode = 0;
    int dst_height  = 0;
    int dst_width   = 0;
    int gravity     = 0;
    std::vector<int> color;

    std::vector<int32_t> calculated_rects;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
