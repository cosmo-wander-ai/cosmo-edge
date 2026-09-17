#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief Decode node for end-to-end YOLO OBB (oriented bounding box) models
 *        (e.g., YOLO26-OBB).
 *
 * End-to-end models have NMS built into the network, so no NMS is needed here.
 * Input:  [batch, box_num, 7]  with (cx, cy, w, h, score, class_id, angle)
 *         in xywh-center format (Ultralytics end2end OBB convention, verified
 *         against YOLO26-OBB exports); angle is the rotation angle in radians.
 * Output: [batch, top_k, 7]   with (cx, cy, w, h, score, class_id, angle),
 *         compatible with the framework's PickDetectionObjects / AdjustSize.
 *         The angle is invariant under coordinate scaling/translation and is
 *         passed through unchanged.
 */
class YoloObbDecodeNode : public Node {
public:
    YoloObbDecodeNode();
    virtual ~YoloObbDecodeNode();

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
    int top_col = 7;  // cx, cy, w, h, score, class_id, angle

    // Net input dimensions for denormalizing coordinates
    int input_width_  = 0;
    int input_height_ = 0;
};

}  // namespace cosmo::nn
