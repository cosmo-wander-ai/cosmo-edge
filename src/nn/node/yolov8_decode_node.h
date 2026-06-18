#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief YoloV8DecodeNode is a node for YOLOv8 decode.
 *
 * It will decode the output of YOLOv8 network.
 * YOLOv8 output is formatted as `(x, y, w, h, c0, c1, ... ,cn)` - NO objectness score!
 * `YoloV8DecodeNode` output is formatted as `(x, y, w, h, conf, class_id)`.
 *
 * Supported input formats:
 * - Format 1: [batch, num_boxes, num_features] - e.g., [1, 8400, 84]
 * - Format 2: [batch, num_features, num_boxes] - e.g., [1, 84, 8400]
 *
 * Key difference from YOLOv5:
 * - YOLOv5: [x, y, w, h, objectness, c0, c1, ...] -> confidence = objectness * max(c_i)
 * - YOLOv8: [x, y, w, h, c0, c1, ...]             -> confidence = max(c_i)
 */
class YoloV8DecodeNode : public Node {
public:
    YoloV8DecodeNode();

    virtual ~YoloV8DecodeNode();

    virtual void LoadParam(Op* op) override;

    virtual Status InferTopShapes() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

private:
    void ResetTopBlob(std::shared_ptr<Blob> top);

    void NMS(std::vector<std::tuple<int, float, int, bool>>& list, float* bottom, int num_features,
             int num_boxes, bool is_format1);

    float IOU(float* box1, float* box2);

private:
    float nms_threshold;
    float base_conf;

    int top_col = 6;
    int top_k;

    // Net input dimensions for denormalizing coordinates
    int input_width_  = 0;
    int input_height_ = 0;
};

}  // namespace cosmo::nn
