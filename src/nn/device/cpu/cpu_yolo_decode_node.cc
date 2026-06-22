#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_yolo_decode_node.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

CpuYoloDecodeNPUNode::CpuYoloDecodeNPUNode() : Node() {
    node_type     = NodeType::NODE_YOLO_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLO_DECODE).append("_0");
    one_blob_only = true;
}

void CpuYoloDecodeNPUNode::LoadParam(Op* op) {
    if (!op)
        return;
    auto yolo_decode = dynamic_cast<YoloNpuPost*>(op);
    if (!yolo_decode)
        return;

    top_k         = yolo_decode->top_k;
    nms_threshold = yolo_decode->nms_threshold;
    base_conf     = yolo_decode->nms_detection_conf;
    anchors       = yolo_decode->anchors;
    stride        = yolo_decode->stride;
}

Status CpuYoloDecodeNPUNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, top_k, top_col}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    return COSMO_NN_OK;
}

size_t CpuYoloDecodeNPUNode::GetBottomCount() {
    return 1;
}

size_t CpuYoloDecodeNPUNode::GetTopCount() {
    return 1;
}

float CpuYoloDecodeNPUNode::Sigmoid(float value) {
    return 1.f / (1.f + std::exp(-value));
}

void CpuYoloDecodeNPUNode::ResetTopBlob(std::shared_ptr<Blob> top_blob) {
    auto top_desc = top_blob->GetBlobDesc();
    auto top_dim  = top_desc.dims;
    top_dim.at(0) = max_batch;
    top_desc.dims = top_dim;
    top_blob->SetBlobDesc(top_desc);

    int count   = DimsVectorUtils::Count(top_dim);
    float* data = static_cast<float*>(top_blob->GetHandle().base);
    std::fill(data, data + count, 0.0f);
}

std::vector<CpuYoloBox> CpuYoloDecodeNPUNode::NMS(std::vector<CpuYoloBox>& detections, float iou_threshold) {
    std::vector<CpuYoloBox> result;
    std::sort(detections.begin(), detections.end(),
              [](const CpuYoloBox& a, const CpuYoloBox& b) { return a.confidence > b.confidence; });

    while (!detections.empty()) {
        result.push_back(detections[0]);
        for (auto it = detections.begin() + 1; it != detections.end();) {
            float x1 = std::max(result.back().x, it->x);
            float y1 = std::max(result.back().y, it->y);
            float x2 = std::min(result.back().x + result.back().width, it->x + it->width);
            float y2 = std::min(result.back().y + result.back().height, it->y + it->height);

            float intersection = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
            float area1        = result.back().width * result.back().height;
            float area2        = it->width * it->height;
            float iou          = intersection / (area1 + area2 - intersection);

            if (iou > iou_threshold)
                it = detections.erase(it);
            else
                ++it;
        }
        detections.erase(detections.begin());
    }
    return result;
}

Status CpuYoloDecodeNPUNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    auto top_blob = top_blobs.at(0);
    ResetTopBlob(top_blob);

    auto top_handle = top_blob->GetHandle();
    float* top_ptr  = static_cast<float*>(top_handle.base);

    std::vector<CpuYoloBox> detections;
    int tidx = 0;

    for (auto& item : bottom_blobs) {
        auto dims         = item->GetBlobDesc().dims;
        auto* tensor_data = static_cast<float*>(item->GetHandle().base);

        int num_anchors = dims[1];
        int grid_height = dims[2];
        int grid_width  = dims[3];
        int num_classes = dims[4] - 5;

        for (int a = 0; a < num_anchors; ++a) {
            for (int h = 0; h < grid_height; ++h) {
                for (int w = 0; w < grid_width; ++w) {
                    int index        = ((a * grid_height + h) * grid_width + w) * (num_classes + 5);
                    float objectness = Sigmoid(tensor_data[index + 4]);

                    if (objectness < base_conf)
                        continue;

                    int best_class  = 0;
                    float best_prob = 0.0f;
                    for (int c = 0; c < num_classes; ++c) {
                        float p = Sigmoid(tensor_data[index + 5 + c]);
                        if (p > best_prob) {
                            best_prob  = p;
                            best_class = c;
                        }
                    }

                    float confidence = objectness * best_prob;
                    if (confidence < base_conf)
                        continue;

                    float dx = Sigmoid(tensor_data[index]);
                    float dy = Sigmoid(tensor_data[index + 1]);
                    float dw = tensor_data[index + 2];
                    float dh = tensor_data[index + 3];

                    float x      = (dx * 2 - 0.5f + w) * stride[tidx];
                    float y      = (dy * 2 - 0.5f + h) * stride[tidx];
                    float width  = std::pow(Sigmoid(dw) * 2, 2) * anchors[tidx][a][0];
                    float height = std::pow(Sigmoid(dh) * 2, 2) * anchors[tidx][a][1];

                    detections.push_back({x, y, width, height, confidence, best_class});
                }
            }
        }
        tidx++;
    }

    auto nms_detections = NMS(detections, nms_threshold);
    int count           = std::min(top_k, static_cast<int>(nms_detections.size()));
    for (int j = 0; j < count; j++) {
        auto& box                = nms_detections[j];
        top_ptr[j * top_col + 0] = box.x;
        top_ptr[j * top_col + 1] = box.y;
        top_ptr[j * top_col + 2] = box.width;
        top_ptr[j * top_col + 3] = box.height;
        top_ptr[j * top_col + 4] = box.confidence;
        top_ptr[j * top_col + 5] = static_cast<float>(box.class_id);
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
