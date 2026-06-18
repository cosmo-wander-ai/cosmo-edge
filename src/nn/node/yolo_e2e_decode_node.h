#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief Decode node for end-to-end YOLO models (e.g., YOLO26).
 *
 * End-to-end models have NMS built into the network, so no NMS is needed here.
 * Input:  [batch, box_num, 6]  with (x1, y1, x2, y2, score, class_id) in xyxy format.
 * Output: [batch, top_k, 6]   with (cx, cy, w, h, score, class_id) in xywh-center format,
 *         compatible with the framework's PickDetectionObjects / AdjustSize.
 */
class YoloE2EDecodeNode : public Node {
public:
    YoloE2EDecodeNode();
    virtual ~YoloE2EDecodeNode();

    virtual void LoadParam(Op* op) override;
    virtual Status InferTopShapes() override;
    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;
    virtual size_t GetBottomCount() override;
    virtual size_t GetTopCount() override;

private:
    void ResetTopBlob(std::shared_ptr<Blob> top);

    float base_conf;
    int top_k;
    int top_col = 6;

    // Net input dimensions for denormalizing coordinates
    int input_width_  = 0;
    int input_height_ = 0;
};

}  // namespace cosmo::nn
