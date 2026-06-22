#pragma once

#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/node/node.h"

// Reuse YoloBox/YoloBoxVec from Sophon header (they are POD, no Sophon deps)
namespace cosmo::nn {

struct CpuYoloBox {
    float x, y, width, height;
    float confidence;
    int class_id;
};

/**
 * @brief CPU YOLO NPU decode node.
 *
 * Decodes YOLO network output (anchor-based format) into bounding boxes.
 * Performs sigmoid, anchor decoding, and NMS entirely on host CPU.
 */
class CpuYoloDecodeNPUNode : public Node {
public:
    CpuYoloDecodeNPUNode();
    ~CpuYoloDecodeNPUNode() override = default;

    void LoadParam(Op* op) override;
    Status InferTopShapes() override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;

    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    void ResetTopBlob(std::shared_ptr<Blob> top);
    std::vector<CpuYoloBox> NMS(std::vector<CpuYoloBox>& detections, float iou_threshold);
    static float Sigmoid(float value);

    float nms_threshold = 0.35f;
    float base_conf     = 0.1f;
    int top_col         = 6;
    int top_k           = 1000;

    std::vector<std::vector<std::vector<float>>> anchors;
    std::vector<float> stride;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
