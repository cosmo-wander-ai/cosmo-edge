#pragma once

#include "nn/device/sophon/sophon_node.h"

namespace cosmo::nn {

/**
 * @brief SophonSequenceNode
 *
 * Sequence node for ascend device.
 *
 */
class SophonSequenceNode : public Node {
public:
    SophonSequenceNode();

    virtual ~SophonSequenceNode();

    virtual void LoadParam(Op* op) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

    virtual DeviceType GetTopBlobDeviceType() override;

    virtual Status InferTopShapes() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blob,
                           std::vector<std::shared_ptr<Blob>>& params,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status PrepareRect(std::shared_ptr<Blob> host_rect, Size image_size);

private:
    float scale;
    bool is_bgr;

    int dst_batch;
    int dst_depth;
    int dst_channels = 3;
    int dst_width;
    int dst_height;

    std::vector<int32_t> calculated_rects;
};

}  // namespace cosmo::nn
