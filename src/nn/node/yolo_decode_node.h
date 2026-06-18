#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

/**
 * @brief YoloDecodeNode is a node for yolo decode.
 *
 * It will decode the output of yolo network.
 * while `YOLO` output is formatted as `(x, y, w, h, obj, c0, c1, ... ,cn)`,
 * `YoloDecodeNode` output is formatted as `(x, y, w, h, conf, class_id)`.
 */
class YoloDecodeNode : public Node {
public:
    YoloDecodeNode();

    virtual ~YoloDecodeNode();

    virtual void LoadParam(Op* op) override;

    virtual Status InferTopShapes() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

private:
    void ResetTopBlob(std::shared_ptr<Blob> top);

    void NMS(std::vector<std::tuple<int, float, int, bool>>& list, float* bottom, int bottom_col);

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
