#include "nn/node/yolo_e2e_decode_node.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"
#include "nn/utils/yolo26_raw_postprocess.h"
#include "util/Log.h"

namespace cosmo::nn {
namespace {

    bool ValidFloatTensor(const std::shared_ptr<Blob>& blob) {
        if (!blob || !blob->GetHandle().base || blob->GetBlobDesc().data_type != DATA_TYPE_FLOAT ||
            !UsesHostMemory(blob->GetBlobDesc().device_type))
            return false;
        const auto& dims = blob->GetBlobDesc().dims;
        if (dims.size() != 3)
            return false;
        size_t count = 1;
        for (int dim : dims) {
            if (dim <= 0 || count > static_cast<size_t>(std::numeric_limits<int>::max()) / dim)
                return false;
            count *= dim;
        }
        return true;
    }

}  // namespace

YoloE2EDecodeNode::YoloE2EDecodeNode() : Node() {
    node_type     = NodeType::NODE_YOLO_E2E_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLO_E2E_DECODE).append("_0");
    one_blob_only = true;
}

YoloE2EDecodeNode::~YoloE2EDecodeNode() {}

void YoloE2EDecodeNode::LoadParam(Op* op) {
    valid_params_ = false;
    auto* post    = dynamic_cast<YoloPost*>(op);
    if (!post)
        return;

    top_k          = post->top_k;
    base_conf      = post->nms_detection_conf;
    nms_threshold_ = post->nms_threshold;
    raw_output_    = post->raw_output;
    input_width_   = post->input_width;
    input_height_  = post->input_height;
    valid_params_ =
        top_k > 0 && std::isfinite(base_conf) && base_conf >= 0.f && base_conf <= 1.f &&
        (!raw_output_ || (std::isfinite(nms_threshold_) && nms_threshold_ >= 0.f && nms_threshold_ <= 1.f));
}

Status YoloE2EDecodeNode::InferTopShapes() {
    if (!valid_params_ || max_batch <= 0 ||
        static_cast<size_t>(top_k) >
            static_cast<size_t>(std::numeric_limits<int>::max()) / top_col / max_batch)
        return Status(COSMO_NN_ERR_PARAM, "Invalid YOLO E2E decoder parameters");
    top_blob_shapes     = {{max_batch, top_k, top_col}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};
    return COSMO_NN_OK;
}

size_t YoloE2EDecodeNode::GetBottomCount() {
    return 1;
}
size_t YoloE2EDecodeNode::GetTopCount() {
    return 1;
}

Status YoloE2EDecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    if (!valid_params_ || bottom_blobs.size() != 1 || top_blobs.size() != 1 ||
        !ValidFloatTensor(bottom_blobs[0]) || !ValidFloatTensor(top_blobs[0]))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "YOLO E2E expects nonempty 3D float32 tensors");

    auto bottom_blob = bottom_blobs[0];
    auto top_blob    = top_blobs[0];
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));
    const auto& bottom_dim = bottom_blob->GetBlobDesc().dims;
    const auto& top_dim    = top_blob->GetBlobDesc().dims;
    const int batch        = bottom_dim[0];
    if ((raw_output_ && bottom_dim[1] <= 4) || (!raw_output_ && bottom_dim[2] != 6))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Invalid YOLO E2E output layout for raw_output mode");
    if (batch > max_batch || top_dim[0] < batch || top_dim[1] != top_k || top_dim[2] != top_col)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "YOLO E2E output capacity is insufficient");

    timer.Start();
    const int top_row       = top_dim[1];
    const float* bottom_ptr = static_cast<const float*>(bottom_blob->GetHandle().base);
    float* top_ptr          = static_cast<float*>(top_blob->GetHandle().base);
    // Keep the declared batch capacity across partial-batch calls. The output
    // parser uses the actual source-image count, and unused batches stay zero.
    std::fill(top_ptr, top_ptr + DimsVectorUtils::Count(top_dim), 0.f);

    // Raw BCN outputs contain pixel center xywh and C-4 class probabilities.
    // Select explicitly: [B, 6, 6] cannot identify the layout from shape alone.
    if (raw_output_) {
        const int channel_count   = bottom_dim.at(1);
        const int candidate_count = bottom_dim.at(2);
        for (int b = 0; b < batch; ++b) {
            const float* src      = bottom_ptr + static_cast<size_t>(b) * channel_count * candidate_count;
            float* dst            = top_ptr + static_cast<size_t>(b) * top_row * top_col;
            const auto detections = DecodeYolo26RawHead(src, static_cast<size_t>(candidate_count),
                                                        static_cast<size_t>(channel_count), base_conf,
                                                        nms_threshold_, static_cast<size_t>(top_k));
            for (size_t i = 0; i < detections.size(); ++i) {
                dst[i * top_col + 0] = detections[i].cx;
                dst[i * top_col + 1] = detections[i].cy;
                dst[i * top_col + 2] = detections[i].width;
                dst[i * top_col + 3] = detections[i].height;
                dst[i * top_col + 4] = detections[i].confidence;
                dst[i * top_col + 5] = static_cast<float>(detections[i].class_id);
            }
        }
        timer.Stop();
        return COSMO_NN_OK;
    }

    int box_num = bottom_dim.at(1);
    int box_col = bottom_dim.at(2);  // should be 6 for decoded output

    for (int b = 0; b < batch; b++) {
        const float* src = bottom_ptr + b * box_num * box_col;
        float* dst       = top_ptr + b * top_row * top_col;

        // Auto-detect normalized coordinates from raw (x1,y1,x2,y2)
        bool is_normalized = false;
        if (input_width_ > 0 && input_height_ > 0) {
            is_normalized   = true;
            int check_count = std::min(20, box_num);
            for (int i = 0; i < check_count && is_normalized; i++) {
                float x1 = src[i * box_col + 0];
                float y1 = src[i * box_col + 1];
                float x2 = src[i * box_col + 2];
                float y2 = src[i * box_col + 3];
                float sc = src[i * box_col + 4];
                if (std::isnan(sc) || sc < base_conf)
                    continue;
                if (x1 > 1.0f || y1 > 1.0f || x2 > 1.0f || y2 > 1.0f) {
                    is_normalized = false;
                }
            }
        }

        if (is_normalized) {
            LOG_DEBUG(
                "[YoloE2EDecodeNode] batch {}: detected normalized coordinates, denormalizing by [{}x{}]", b,
                input_width_, input_height_);
        }

        int valid_count = 0;
        for (int i = 0; i < box_num && valid_count < top_k; i++) {
            float x1       = src[i * box_col + 0];
            float y1       = src[i * box_col + 1];
            float x2       = src[i * box_col + 2];
            float y2       = src[i * box_col + 3];
            float score    = src[i * box_col + 4];
            float class_id = src[i * box_col + 5];

            if (std::isnan(score) || score < base_conf)
                continue;

            // Denormalize if model outputs normalized coordinates
            if (is_normalized) {
                x1 *= input_width_;
                y1 *= input_height_;
                x2 *= input_width_;
                y2 *= input_height_;
            }

            // xyxy → xywh center format for framework compatibility
            float cx = (x1 + x2) * 0.5f;
            float cy = (y1 + y2) * 0.5f;
            float w  = x2 - x1;
            float h  = y2 - y1;

            dst[valid_count * top_col + 0] = cx;
            dst[valid_count * top_col + 1] = cy;
            dst[valid_count * top_col + 2] = w;
            dst[valid_count * top_col + 3] = h;
            dst[valid_count * top_col + 4] = score;
            dst[valid_count * top_col + 5] = class_id;
            valid_count++;
        }
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
