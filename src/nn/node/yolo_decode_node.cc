#include "nn/node/yolo_decode_node.h"

#include <algorithm>
#include <cmath>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "util/Log.h"

namespace cosmo::nn {

YoloDecodeNode::YoloDecodeNode() : Node() {
    node_type     = NodeType::NODE_YOLO_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLO_DECODE).append("_0");
    one_blob_only = true;
}

YoloDecodeNode::~YoloDecodeNode() {}

void YoloDecodeNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto yolo_decode = dynamic_cast<YoloPost*>(op);

    top_k         = yolo_decode->top_k;
    nms_threshold = yolo_decode->nms_threshold;
    base_conf     = yolo_decode->nms_detection_conf;
    input_width_  = yolo_decode->input_width;
    input_height_ = yolo_decode->input_height;
}

Status YoloDecodeNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, top_k, top_col}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};

    return COSMO_NN_OK;
}

size_t YoloDecodeNode::GetBottomCount() {
    return 1;
}

size_t YoloDecodeNode::GetTopCount() {
    return 1;
}

Status YoloDecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
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
    int batch      = bottom_dim.at(0);
    int bottom_row = bottom_dim.at(1);
    int bottom_col = bottom_dim.at(2);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    int class_num = bottom_col - 5;

    // top blob set current shape
    SetCurrentBatch(top_blob, batch);

    auto top_dim    = top_blob->GetBlobDesc().dims;
    auto top_handle = top_blob->GetHandle();

    int top_row = top_dim.at(1);

    float* bottom_ptr = reinterpret_cast<float*>(bottom_handle.base);
    float* top_ptr    = reinterpret_cast<float*>(top_handle.base);

    for (int i = 0; i < batch; i++) {
        auto bottom_ptr_i = bottom_ptr + i * bottom_row * bottom_col;
        auto top_ptr_i    = top_ptr + i * top_row * top_col;

        std::vector<std::tuple<int, float, int, bool>> conf_list;
        for (int j = 0; j < bottom_row; j++) {
            float objness = bottom_ptr_i[j * bottom_col + 4];

            auto conf_start    = bottom_ptr_i + j * bottom_col + 5;
            auto conf_end      = conf_start + class_num;
            auto max_conf_iter = std::max_element(conf_start, conf_end);
            int class_id       = static_cast<int>(std::distance(conf_start, max_conf_iter));
            float confidence   = objness * (*max_conf_iter);

            if (std::isnan(confidence))
                continue;

            if (confidence < base_conf)
                continue;

            conf_list.push_back(std::make_tuple(j, confidence, class_id, true));
        }

        NMS(conf_list, bottom_ptr_i, bottom_col);

        // Auto-detect normalized coordinates
        bool is_normalized = false;
        if (input_width_ > 0 && input_height_ > 0 && !conf_list.empty()) {
            is_normalized   = true;
            int check_count = std::min(20, (int)conf_list.size());
            for (int j = 0; j < check_count && is_normalized; j++) {
                if (!std::get<3>(conf_list.at(j)))
                    continue;
                int idx  = std::get<0>(conf_list.at(j));
                float cx = bottom_ptr_i[idx * bottom_col + 0];
                float cy = bottom_ptr_i[idx * bottom_col + 1];
                float cw = bottom_ptr_i[idx * bottom_col + 2];
                float ch = bottom_ptr_i[idx * bottom_col + 3];
                if (cx > 1.0f || cy > 1.0f || cw > 1.0f || ch > 1.0f) {
                    is_normalized = false;
                }
            }
        }

        if (is_normalized) {
            LOG_DEBUG("[YoloDecodeNode] batch {}: detected normalized coordinates, denormalizing by [{}x{}]",
                     i, input_width_, input_height_);
        }

        int min = std::min(top_k, static_cast<int>(conf_list.size()));
        for (int j = 0; j < min; j++) {
            if (!std::get<3>(conf_list.at(j)))
                continue;

            int index = std::get<0>(conf_list.at(j));
            float x   = bottom_ptr_i[index * bottom_col + 0];
            float y   = bottom_ptr_i[index * bottom_col + 1];
            float w   = bottom_ptr_i[index * bottom_col + 2];
            float h   = bottom_ptr_i[index * bottom_col + 3];

            // Denormalize if model outputs normalized coordinates
            if (is_normalized) {
                x *= input_width_;
                y *= input_height_;
                w *= input_width_;
                h *= input_height_;
            }

            top_ptr_i[j * top_col + 0] = x;
            top_ptr_i[j * top_col + 1] = y;
            top_ptr_i[j * top_col + 2] = w;
            top_ptr_i[j * top_col + 3] = h;
            top_ptr_i[j * top_col + 4] = std::get<1>(conf_list.at(j));
            top_ptr_i[j * top_col + 5] = std::get<2>(conf_list.at(j));
        }
    }

    timer.Stop();

    return COSMO_NN_OK;
}

void YoloDecodeNode::NMS(std::vector<std::tuple<int, float, int, bool>>& conf_list, float* bottom,
                         int bottom_col) {
    std::sort(conf_list.begin(), conf_list.end(),
              [](const std::tuple<int, float, int, bool>& a, const std::tuple<int, float, int, bool>& b) {
                  return std::get<1>(a) > std::get<1>(b);
              });

    for (int i = 0; i < conf_list.size(); i++) {
        if (!std::get<3>(conf_list.at(i)))
            continue;

        int class_id = std::get<2>(conf_list.at(i));
        for (int j = i + 1; j < conf_list.size(); j++) {
            if (!std::get<3>(conf_list.at(j)))
                continue;

            if (class_id != std::get<2>(conf_list.at(j)))
                continue;

            float* box1 = bottom + std::get<0>(conf_list.at(i)) * bottom_col;
            float* box2 = bottom + std::get<0>(conf_list.at(j)) * bottom_col;
            float iou   = IOU(box1, box2);
            if (iou >= nms_threshold) {
                std::get<3>(conf_list.at(j)) = false;
            }
        }
    }
}

float YoloDecodeNode::IOU(float* box1, float* box2) {
    float center_x_1 = box1[0];
    float center_y_1 = box1[1];
    float width_1    = box1[2];
    float height_1   = box1[3];
    float left_1     = center_x_1 - width_1 / 2;
    float right_1    = center_x_1 + width_1 / 2;
    float top_1      = center_y_1 - height_1 / 2;
    float bottom_1   = center_y_1 + height_1 / 2;

    float center_x_2 = box2[0];
    float center_y_2 = box2[1];
    float width_2    = box2[2];
    float height_2   = box2[3];
    float left_2     = center_x_2 - width_2 / 2;
    float right_2    = center_x_2 + width_2 / 2;
    float top_2      = center_y_2 - height_2 / 2;
    float bottom_2   = center_y_2 + height_2 / 2;

    float interBox[] = {(std::max)(left_1, left_2), (std::min)(right_1, right_2), (std::max)(top_1, top_2),
                        (std::min)(bottom_1, bottom_2)};

    if (interBox[2] > interBox[3] || interBox[0] > interBox[1])
        return 0.0f;

    float interBoxS = (interBox[1] - interBox[0]) * (interBox[3] - interBox[2]);
    return interBoxS / (width_1 * height_1 + width_2 * height_2 - interBoxS);
}

void YoloDecodeNode::ResetTopBlob(std::shared_ptr<Blob> top_blob) {
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
