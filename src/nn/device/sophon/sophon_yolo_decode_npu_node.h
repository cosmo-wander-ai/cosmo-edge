#pragma once

#include "nn/node/node.h"

namespace cosmo::nn {

struct YoloBox {
    float x, y, width, height;
    float confidence;
    int class_id;
};

using YoloBoxVec = std::vector<YoloBox>;

/**
 * @brief YoloDecodeNode is a node for yolo decode.
 *
 * It will decode the output of yolo network.
 * while `YOLO` output is formatted as `(x, y, w, h, obj, c0, c1, ... ,cn)`,
 * `YoloDecodeNode` output is formatted as `(x, y, w, h, conf, class_id)`.
 */
class SophonYoloDecodeNPUNode : public Node {
public:
    SophonYoloDecodeNPUNode();

    virtual ~SophonYoloDecodeNPUNode();

    virtual void LoadParam(Op* op) override;

    virtual Status InferTopShapes() override;

    virtual Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                           std::vector<std::shared_ptr<Blob>>& top_blobs) override;

    virtual size_t GetBottomCount() override;

    virtual size_t GetTopCount() override;

private:
    void ResetTopBlob(std::shared_ptr<Blob> top);

    std::vector<YoloBox> NMS(std::vector<YoloBox>& detections, float iou_threshold);

    float Sigmoid(float value);

private:
    float nms_threshold;
    float base_conf;

    int top_col = 6;
    int top_k;

    std::vector<std::vector<std::vector<float>>> anchors{};
    std::vector<float> stride{};
};

}  // namespace cosmo::nn
