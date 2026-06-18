#include "nn/device/sophon/sophon_yolo_decode_npu_node.h"

#include <string.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "nn/node/node_type_utils.h"
#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

SophonYoloDecodeNPUNode::SophonYoloDecodeNPUNode() : Node() {
    node_type     = NodeType::NODE_YOLO_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLO_DECODE).append("_0");
    one_blob_only = true;
}

SophonYoloDecodeNPUNode::~SophonYoloDecodeNPUNode() {}

void SophonYoloDecodeNPUNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto yolo_decode = dynamic_cast<YoloNpuPost*>(op);

    top_k         = yolo_decode->top_k;
    nms_threshold = yolo_decode->nms_threshold;
    base_conf     = yolo_decode->nms_detection_conf;
    anchors       = yolo_decode->anchors;
    stride        = yolo_decode->stride;
}

Status SophonYoloDecodeNPUNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, top_k, top_col}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};

    return COSMO_NN_OK;
}

size_t SophonYoloDecodeNPUNode::GetBottomCount() {
    return 1;
}

size_t SophonYoloDecodeNPUNode::GetTopCount() {
    return 1;
}

float SophonYoloDecodeNPUNode::Sigmoid(float value) {
    return 1.f / (1.f + std::exp(-value));
}

Status SophonYoloDecodeNPUNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                        std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    ResetTopBlob(top_blob);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dim    = bottom_desc.dims;

    // check batch
    int batch = bottom_dim.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    // top blob set current shape
    SetCurrentBatch(top_blob, batch);

    auto top_dim    = top_blob->GetBlobDesc().dims;
    auto top_handle = top_blob->GetHandle();

    float* top_ptr = reinterpret_cast<float*>(top_handle.base);

    std::vector<YoloBox> detections;
    int tidx = 0;
    for (auto item : bottom_blobs) {
        auto handle      = item->GetHandle();
        auto desc        = item->GetBlobDesc();
        auto dims        = item->GetBlobDesc().dims;
        auto tensor_data = reinterpret_cast<float*>(handle.base);
        int batch_size   = dims[0];
        int num_anchors  = dims[1];
        int grid_height  = dims[2];
        int grid_width   = dims[3];
        int num_classes  = dims[4] - 5;

        for (int a = 0; a < num_anchors; ++a) {
            for (int h = 0; h < grid_height; ++h) {
                for (int w = 0; w < grid_width; ++w) {
                    int index        = ((a * grid_height + h) * grid_width + w) * (num_classes + 5);
                    float objectness = Sigmoid(tensor_data[index + 4]);

                    if (objectness < base_conf) {
                        continue;
                    }

                    std::vector<float> class_probs(num_classes);
                    for (int c = 0; c < num_classes; ++c) {
                        class_probs[c] = Sigmoid(tensor_data[index + 5 + c]);
                    }

                    auto max_iter    = std::max_element(class_probs.begin(), class_probs.end());
                    int class_id     = std::distance(class_probs.begin(), max_iter);
                    float max_prob   = *max_iter;
                    float confidence = objectness * max_prob;
                    if (confidence < base_conf) {
                        continue;
                    }

                    float dx = Sigmoid(tensor_data[index]);
                    float dy = Sigmoid(tensor_data[index + 1]);
                    float dw = tensor_data[index + 2];
                    float dh = tensor_data[index + 3];

                    float x      = (dx * 2 - 0.5 + w) * stride[tidx];
                    float y      = (dy * 2 - 0.5 + h) * stride[tidx];
                    float width  = std::pow(Sigmoid(dw) * 2, 2) * anchors[tidx][a][0];
                    float height = std::pow(Sigmoid(dh) * 2, 2) * anchors[tidx][a][1];

                    detections.push_back({x, y, width, height, confidence, class_id});
                }
            }
        }
        tidx++;
    }

    std::vector<YoloBox> nms_detections = NMS(detections, nms_threshold);
    int min                             = std::min(top_k, static_cast<int>(nms_detections.size()));
    for (int j = 0; j < min; j++) {
        auto box                 = nms_detections.at(j);
        top_ptr[j * top_col + 0] = box.x;
        top_ptr[j * top_col + 1] = box.y;
        top_ptr[j * top_col + 2] = box.width;
        top_ptr[j * top_col + 3] = box.height;
        top_ptr[j * top_col + 4] = box.confidence;
        top_ptr[j * top_col + 5] = box.class_id;
    }
    timer.Stop();

    return COSMO_NN_OK;
}

std::vector<YoloBox> SophonYoloDecodeNPUNode::NMS(std::vector<YoloBox>& detections, float iou_threshold) {
    std::vector<YoloBox> result;
    std::sort(detections.begin(), detections.end(),
              [](const YoloBox& a, const YoloBox& b) { return a.confidence > b.confidence; });

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

            if (iou > iou_threshold) {
                it = detections.erase(it);
            } else {
                ++it;
            }
        }
        detections.erase(detections.begin());
    }

    return result;
}

void SophonYoloDecodeNPUNode::ResetTopBlob(std::shared_ptr<Blob> top_blob) {
    auto top_desc = top_blob->GetBlobDesc();
    auto top_dim  = top_desc.dims;
    top_dim.at(0) = max_batch;
    top_desc.dims = top_dim;
    top_blob->SetBlobDesc(top_desc);

    int count = DimsVectorUtils::Count(top_dim);

    auto handle = top_blob->GetHandle();
    float* data = static_cast<float*>(handle.base);
    std::fill(data, data + count, 0);
}

}  // namespace cosmo::nn
