#include "nn/node/yolo_obb_decode_node.h"

#include <algorithm>
#include <cmath>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"
#include "util/Log.h"

namespace cosmo::nn {

YoloObbDecodeNode::YoloObbDecodeNode() : Node() {
    node_type     = NodeType::NODE_YOLO_OBB_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLO_OBB_DECODE).append("_0");
    one_blob_only = true;
}

YoloObbDecodeNode::~YoloObbDecodeNode() {}

void YoloObbDecodeNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto* post    = dynamic_cast<YoloPost*>(op);
    top_k         = post->top_k;
    base_conf     = post->nms_detection_conf;
    input_width_  = post->input_width;
    input_height_ = post->input_height;
}

Status YoloObbDecodeNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, top_k, top_col}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    return COSMO_NN_OK;
}

size_t YoloObbDecodeNode::GetBottomCount() {
    return 1;
}
size_t YoloObbDecodeNode::GetTopCount() {
    return 1;
}

Status YoloObbDecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    timer.Start();

    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    ResetTopBlob(top_blob);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dim    = bottom_desc.dims;

    int batch   = bottom_dim.at(0);
    int box_num = bottom_dim.at(1);
    int box_col = bottom_dim.at(2);  // should be 7 (x1, y1, x2, y2, score, class_id, angle)
    if (box_col < top_col)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "OBB output requires at least 7 columns");
    if (batch > static_cast<int>(max_batch))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    SetCurrentBatch(top_blob, batch);

    auto top_dim    = top_blob->GetBlobDesc().dims;
    auto top_handle = top_blob->GetHandle();
    int top_row     = top_dim.at(1);

    float* bottom_ptr = reinterpret_cast<float*>(bottom_handle.base);
    float* top_ptr    = reinterpret_cast<float*>(top_handle.base);

    for (int b = 0; b < batch; b++) {
        float* src = bottom_ptr + b * box_num * box_col;
        float* dst = top_ptr + b * top_row * top_col;

        // Ultralytics end-to-end OBB outputs (cx, cy, w, h) directly.
        // Auto-detect normalized coordinates: if all four box columns of the
        // sampled confident rows are within [0,1], treat them as normalized.
        bool is_normalized = false;
        if (input_width_ > 0 && input_height_ > 0) {
            is_normalized   = true;
            int check_count = std::min(20, box_num);
            for (int i = 0; i < check_count && is_normalized; i++) {
                float cx = src[i * box_col + 0];
                float cy = src[i * box_col + 1];
                float w  = src[i * box_col + 2];
                float h  = src[i * box_col + 3];
                float sc = src[i * box_col + 4];
                if (std::isnan(sc) || sc < base_conf)
                    continue;
                if (cx > 1.0f || cy > 1.0f || w > 1.0f || h > 1.0f) {
                    is_normalized = false;
                }
            }
        }

        if (is_normalized) {
            LOG_DEBUG(
                "[YoloObbDecodeNode] batch {}: detected normalized coordinates, denormalizing by [{}x{}]",
                b, input_width_, input_height_);
        }

        int valid_count = 0;
        for (int i = 0; i < box_num && valid_count < top_k; i++) {
            float cx       = src[i * box_col + 0];
            float cy       = src[i * box_col + 1];
            float w        = src[i * box_col + 2];
            float h        = src[i * box_col + 3];
            float score    = src[i * box_col + 4];
            float class_id = src[i * box_col + 5];
            float angle    = src[i * box_col + 6];

            if (std::isnan(score) || score < base_conf)
                continue;
            if (w <= 0.0f || h <= 0.0f)
                continue;

            // Denormalize if model outputs normalized coordinates
            if (is_normalized) {
                cx *= input_width_;
                cy *= input_height_;
                w *= input_width_;
                h *= input_height_;
            }

            // (cx, cy, w, h) center format passes through unchanged; the
            // rotation angle is invariant under scaling/translation.
            dst[valid_count * top_col + 0] = cx;
            dst[valid_count * top_col + 1] = cy;
            dst[valid_count * top_col + 2] = w;
            dst[valid_count * top_col + 3] = h;
            dst[valid_count * top_col + 4] = score;
            dst[valid_count * top_col + 5] = class_id;
            dst[valid_count * top_col + 6] = angle;
            valid_count++;
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

void YoloObbDecodeNode::ResetTopBlob(std::shared_ptr<Blob> top_blob) {
    auto top_desc = top_blob->GetBlobDesc();
    auto top_dim  = top_desc.dims;
    top_dim.at(0) = max_batch;
    top_desc.dims = top_dim;
    top_blob->SetBlobDesc(top_desc);

    int count   = DimsVectorUtils::Count(top_dim);
    auto handle = top_blob->GetHandle();
    float* data = static_cast<float*>(handle.base);
    std::fill(data, data + count, 0);
}

}  // namespace cosmo::nn
