#pragma once

#include "nn/device/sophon/sophon_copy_node.h"
#include "nn/node/node.h"
#include "nn/utils/net_utils.h"

namespace cosmo::nn {

/**
 * SophonCropResizeNode
 *
 * Sophon implementation of crop resize node
 */
class SophonCropResizeNode : public Node {
public:
    SophonCropResizeNode();

    virtual ~SophonCropResizeNode();

    virtual void LoadParam(Op* op) override;

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual Status InferTopShapes() override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    /**
     * @brief Forward
     *
     * @param bottom_blob. [image blob]
     * @param params. [rect blob]
     * @param top_blobs [crop blob]
     * @return Status
     */
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                           std::vector<std::shared_ptr<Blob>>& params,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    /**
     * @brief Forward
     *
     * @param bottom_blobs. [image blob, rect blob]
     * @param top_blobs [crop blob]
     * @return Status
     */
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& rect_blobs, std::shared_ptr<Blob> top_blob);

    Status PrepareRect(std::shared_ptr<Blob> host_rect, Size image_size);

private:
    std::string type                        = {};
    std::vector<float> bbox_hw_ratio_levels = {};
    std::vector<float> h_top_crop           = {};
    std::vector<float> h_bottom_crop        = {};
    std::vector<float> w_left_crop          = {};
    std::vector<float> w_right_crop         = {};

    bool square     = false;
    int square_mode = 0;

    int dst_height         = 0;
    int dst_width          = 0;
    int gravity            = 0;
    std::vector<int> color = {};

    /**
     * (x, y, w, h)
     */
    int rect_data_col     = 4;
    int32_t* device_rects = nullptr;

    std::vector<int32_t> calculated_rects;
};

}  // namespace cosmo::nn
