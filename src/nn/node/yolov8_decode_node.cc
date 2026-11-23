#include "nn/node/yolov8_decode_node.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "nn/core/inference_pipeline_metrics.h"

#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
#include "nn/device/axera/axera_face_diagnostics.h"
#endif
#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"
#include "util/Log.h"

namespace cosmo::nn {
namespace {

    using MetricsClock = std::chrono::steady_clock;

    uint64_t ElapsedNanoseconds(MetricsClock::time_point started_at) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(MetricsClock::now() - started_at).count());
    }

    class ScopedPostprocessMetric {
    public:
        ScopedPostprocessMetric() : started_at_(MetricsClock::now()) {}
        ~ScopedPostprocessMetric() {
            GetInferencePipelineMetrics().RecordYolov8Postprocess(ElapsedNanoseconds(started_at_));
        }

    private:
        MetricsClock::time_point started_at_;
    };

}  // namespace

YoloV8DecodeNode::YoloV8DecodeNode() : Node() {
    node_type     = NodeType::NODE_YOLOV8_DECODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_YOLOV8_DECODE).append("_0");
    one_blob_only = true;
}

YoloV8DecodeNode::~YoloV8DecodeNode() {}

void YoloV8DecodeNode::LoadParam(Op* op) {
    if (!op)
        return;

    // YOLOv8 uses YoloPost with the same parameters
    auto yolov8_decode = dynamic_cast<YoloPost*>(op);

    top_k         = yolov8_decode->top_k;
    nms_threshold = yolov8_decode->nms_threshold;
    base_conf     = yolov8_decode->nms_detection_conf;
    input_width_  = yolov8_decode->input_width;
    input_height_ = yolov8_decode->input_height;
}

Status YoloV8DecodeNode::InferTopShapes() {
    top_blob_shapes     = {{max_batch, top_k, top_col}};
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT};

    if (shared_resource) {
        // Keep rectangular RKNN inputs on the existing tensor decode path.
        shared_resource->yolov8_direct_postprocess = {input_width_ == input_height_, base_conf, input_width_,
                                                      input_height_};
    }

    return Status{COSMO_NN_OK};
}

size_t YoloV8DecodeNode::GetBottomCount() {
    return 1;
}

size_t YoloV8DecodeNode::GetTopCount() {
    return 1;
}

Status YoloV8DecodeNode::Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                                 std::vector<std::shared_ptr<Blob>>& top_blobs) {
    ScopedPostprocessMetric postprocess_metric;
    timer.Start();

    auto bottom_blob = bottom_blobs.at(0);
    auto top_blob    = top_blobs.at(0);
    ResetTopBlob(top_blob);
    RETURN_ON_FAIL(CheckNodeInputOutput(bottom_blob, top_blob, true));

    if (shared_resource && shared_resource->yolov8_candidate_batch.ready) {
        const auto status = ForwardDirectCandidates(top_blob);
        timer.Stop();
        return status;
    }

    auto bottom_desc   = bottom_blob->GetBlobDesc();
    auto bottom_handle = bottom_blob->GetHandle();
    auto bottom_dim    = bottom_desc.dims;

    // check batch
    int batch = bottom_dim.at(0);
    int dim1  = bottom_dim.at(1);
    int dim2  = bottom_dim.at(2);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    // Detect input format: [batch, num_boxes, num_features] or [batch, num_features, num_boxes]
    // Format 1: [1, 8400, 84] - num_boxes=8400, num_features=84
    // Format 2: [1, 84, 8400] - num_features=84, num_boxes=8400
    bool is_format1 = (dim1 > dim2);  // num_boxes > num_features (typical case)
    int num_boxes, num_features;
    if (is_format1) {
        num_boxes    = dim1;  // e.g., 8400
        num_features = dim2;  // e.g., 84
    } else {
        num_features = dim1;  // e.g., 84
        num_boxes    = dim2;  // e.g., 8400
    }

    // YOLOv8: no objectness, class_num = num_features - 4
    int class_num = num_features - 4;
    if (class_num <= 0) {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "invalid num_features, should be >= 5");
    }

    LOG_DEBUG(
        "[YoloV8DecodeNode] bottom_dim: [{}, {}, {}], is_format1: {}, num_boxes: {}, num_features: {}, "
        "class_num: {}, base_conf: {}, input_size: [{}x{}]",
        batch, dim1, dim2, (int)is_format1, num_boxes, num_features, class_num, base_conf, input_width_,
        input_height_);

    // top blob set current shape
    SetCurrentBatch(top_blob, batch);

    auto top_dim    = top_blob->GetBlobDesc().dims;
    auto top_handle = top_blob->GetHandle();

    int top_row = top_dim.at(1);

    float* bottom_ptr = reinterpret_cast<float*>(bottom_handle.base);
    float* top_ptr    = reinterpret_cast<float*>(top_handle.base);

    for (int i = 0; i < batch; i++) {
        auto bottom_ptr_i = bottom_ptr + i * dim1 * dim2;
        auto top_ptr_i    = top_ptr + i * top_row * top_col;

        std::vector<std::tuple<int, float, int, bool>> conf_list;
        conf_list.reserve(static_cast<size_t>(num_boxes));
        float global_max_conf = -1.0f;
        const bool class_major_scan =
            !is_format1 && shared_resource && shared_resource->prefer_yolov8_class_major_scan;
        if (class_major_scan) {
            class_max_scratch_.assign(static_cast<size_t>(num_boxes), -1.0f);
            class_id_scratch_.assign(static_cast<size_t>(num_boxes), -1);
            for (int c = 0; c < class_num; ++c) {
                const float* scores = bottom_ptr_i + (4 + c) * num_boxes;
                for (int j = 0; j < num_boxes; ++j) {
                    if (scores[j] > class_max_scratch_[j]) {
                        class_max_scratch_[j] = scores[j];
                        class_id_scratch_[j]  = c;
                    }
                }
            }
            for (int j = 0; j < num_boxes; ++j) {
                const float confidence = class_max_scratch_[j];
                global_max_conf        = std::max(global_max_conf, confidence);
                if (!std::isnan(confidence) && confidence >= base_conf) {
                    conf_list.emplace_back(j, confidence, class_id_scratch_[j], true);
                }
            }
        } else {
            for (int j = 0; j < num_boxes; j++) {
                if (is_format1) {
                    // Format 1: [batch, num_boxes, num_features]
                    // Data layout: box0_features, box1_features, ...
                    float* box_data   = bottom_ptr_i + j * num_features;
                    float* conf_start = box_data + 4;
                    float max_conf    = -1.0f;
                    int class_id      = -1;
                    for (int c = 0; c < class_num; c++) {
                        if (conf_start[c] > max_conf) {
                            max_conf = conf_start[c];
                            class_id = c;
                        }
                    }
                    global_max_conf = std::max(global_max_conf, max_conf);
                    if (!std::isnan(max_conf) && max_conf >= base_conf)
                        conf_list.emplace_back(j, max_conf, class_id, true);
                } else {
                    // Established format-2 path, retained for non-capable producers.
                    float max_conf = -1.0f;
                    int class_id   = -1;
                    for (int c = 0; c < class_num; c++) {
                        const float conf = bottom_ptr_i[(4 + c) * num_boxes + j];
                        if (conf > max_conf) {
                            max_conf = conf;
                            class_id = c;
                        }
                    }
                    global_max_conf = std::max(global_max_conf, max_conf);
                    if (!std::isnan(max_conf) && max_conf >= base_conf)
                        conf_list.emplace_back(j, max_conf, class_id, true);
                }
            }
        }

        LOG_DEBUG(
            "[YoloV8DecodeNode] batch {}: global max confidence = {}, total boxes passing base_conf ({}) = "
            "{}",
            i, global_max_conf, base_conf, (int)conf_list.size());

        // Sort pre-NMS list to show the top 5 highest confidence boxes
        std::sort(conf_list.begin(), conf_list.end(),
                  [](const std::tuple<int, float, int, bool>& a, const std::tuple<int, float, int, bool>& b) {
                      return std::get<1>(a) > std::get<1>(b);
                  });

        int pre_nms_print_count = std::min(5, (int)conf_list.size());
        for (int p = 0; p < pre_nms_print_count; p++) {
            int idx    = std::get<0>(conf_list.at(p));
            float conf = std::get<1>(conf_list.at(p));
            int cid    = std::get<2>(conf_list.at(p));
            float x, y, w, h;
            if (is_format1) {
                x = bottom_ptr_i[idx * num_features + 0];
                y = bottom_ptr_i[idx * num_features + 1];
                w = bottom_ptr_i[idx * num_features + 2];
                h = bottom_ptr_i[idx * num_features + 3];
            } else {
                x = bottom_ptr_i[0 * num_boxes + idx];
                y = bottom_ptr_i[1 * num_boxes + idx];
                w = bottom_ptr_i[2 * num_boxes + idx];
                h = bottom_ptr_i[3 * num_boxes + idx];
            }
            LOG_DEBUG(
                "[YoloV8DecodeNode] pre-NMS top {}: idx={}, conf={}, class={}, box_center=[{}, {}], "
                "size=[{}, {}]",
                p, idx, conf, cid, x, y, w, h);
        }

        const auto nms_started = MetricsClock::now();
        NMS(conf_list, bottom_ptr_i, num_features, num_boxes, is_format1);
        GetInferencePipelineMetrics().RecordYolov8Nms(ElapsedNanoseconds(nms_started));

        // Auto-detect normalized coordinates: if all box values are in [0, 1],
        // the model outputs normalized coords and we need to scale by input_size.
        bool is_normalized = false;
        if (input_width_ > 0 && input_height_ > 0 && !conf_list.empty()) {
            is_normalized   = true;
            int check_count = std::min(20, (int)conf_list.size());
            for (int j = 0; j < check_count && is_normalized; j++) {
                if (!std::get<3>(conf_list.at(j)))
                    continue;
                int idx = std::get<0>(conf_list.at(j));
                float cx, cy, cw, ch;
                if (is_format1) {
                    cx = bottom_ptr_i[idx * num_features + 0];
                    cy = bottom_ptr_i[idx * num_features + 1];
                    cw = bottom_ptr_i[idx * num_features + 2];
                    ch = bottom_ptr_i[idx * num_features + 3];
                } else {
                    cx = bottom_ptr_i[0 * num_boxes + idx];
                    cy = bottom_ptr_i[1 * num_boxes + idx];
                    cw = bottom_ptr_i[2 * num_boxes + idx];
                    ch = bottom_ptr_i[3 * num_boxes + idx];
                }
                if (cx > 1.0f || cy > 1.0f || cw > 1.0f || ch > 1.0f) {
                    is_normalized = false;
                }
            }
        }

        LOG_DEBUG("[YoloV8DecodeNode] batch {}: is_normalized = {}", i, (int)is_normalized);

        int min         = std::min(top_k, static_cast<int>(conf_list.size()));
        int valid_count = 0;
        for (int j = 0; j < static_cast<int>(conf_list.size()) && valid_count < min; j++) {
            if (!std::get<3>(conf_list.at(j)))
                continue;

            int index = std::get<0>(conf_list.at(j));
            float x, y, w, h;

            if (is_format1) {
                // Format 1: [batch, num_boxes, num_features]
                x = bottom_ptr_i[index * num_features + 0];
                y = bottom_ptr_i[index * num_features + 1];
                w = bottom_ptr_i[index * num_features + 2];
                h = bottom_ptr_i[index * num_features + 3];
            } else {
                // Format 2: [batch, num_features, num_boxes]
                x = bottom_ptr_i[0 * num_boxes + index];
                y = bottom_ptr_i[1 * num_boxes + index];
                w = bottom_ptr_i[2 * num_boxes + index];
                h = bottom_ptr_i[3 * num_boxes + index];
            }

            // Denormalize if model outputs normalized coordinates
            if (is_normalized) {
                x *= input_width_;
                y *= input_height_;
                w *= input_width_;
                h *= input_height_;
            }

            top_ptr_i[valid_count * top_col + 0] = x;
            top_ptr_i[valid_count * top_col + 1] = y;
            top_ptr_i[valid_count * top_col + 2] = w;
            top_ptr_i[valid_count * top_col + 3] = h;
            top_ptr_i[valid_count * top_col + 4] = std::get<1>(conf_list.at(j));
            top_ptr_i[valid_count * top_col + 5] = std::get<2>(conf_list.at(j));

            if (valid_count < 5) {
                LOG_DEBUG(
                    "[YoloV8DecodeNode] post-NMS top {}: index={}, conf={}, class={}, box_center=[{}, {}], "
                    "size=[{}, {}]",
                    valid_count, index, std::get<1>(conf_list.at(j)), std::get<2>(conf_list.at(j)), x, y, w,
                    h);
            }
            valid_count++;
        }
    }

    timer.Stop();

    return Status{COSMO_NN_OK};
}

Status YoloV8DecodeNode::ForwardDirectCandidates(std::shared_ptr<Blob>& top_blob) {
    auto& batch      = shared_resource->yolov8_candidate_batch;
    batch.ready      = false;
    auto& candidates = batch.candidates;
    SetCurrentBatch(top_blob, 1);

    std::sort(candidates.begin(), candidates.end(),
              [](const Yolov8Candidate& lhs, const Yolov8Candidate& rhs) {
                  return lhs.confidence > rhs.confidence;
              });
    const auto nms_started = MetricsClock::now();
    NmsDirectCandidates(candidates);
    GetInferencePipelineMetrics().RecordYolov8Nms(ElapsedNanoseconds(nms_started));

    auto* output = static_cast<float*>(top_blob->GetHandle().base);
    if (!output)
        return Status(COSMO_NN_ERR_NULL_PARAM, "YOLOv8 direct candidate output is null");
    const int limit = std::min(top_k, static_cast<int>(candidates.size()));
    int valid_count = 0;
    for (const auto& candidate : candidates) {
        if (valid_count >= limit)
            break;
        if (!std::isfinite(candidate.confidence) || candidate.confidence < 0.0f)
            continue;
        output[valid_count * top_col + 0] = candidate.x;
        output[valid_count * top_col + 1] = candidate.y;
        output[valid_count * top_col + 2] = candidate.width;
        output[valid_count * top_col + 3] = candidate.height;
        output[valid_count * top_col + 4] = candidate.confidence;
        output[valid_count * top_col + 5] = static_cast<float>(candidate.class_id);
        ++valid_count;
    }
    return Status{COSMO_NN_OK};
}

void YoloV8DecodeNode::NmsDirectCandidates(std::vector<Yolov8Candidate>& candidates) {
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    cosmo::nn::axera::diagnostics::RecordCpuNms();
#endif
    for (size_t i = 0; i < candidates.size(); ++i) {
        auto& lhs = candidates[i];
        if (lhs.confidence < 0.0f)
            continue;
        for (size_t j = i + 1; j < candidates.size(); ++j) {
            auto& rhs = candidates[j];
            if (rhs.confidence < 0.0f || lhs.class_id != rhs.class_id)
                continue;
            float lhs_box[4]{lhs.x, lhs.y, lhs.width, lhs.height};
            float rhs_box[4]{rhs.x, rhs.y, rhs.width, rhs.height};
            if (IOU(lhs_box, rhs_box) >= nms_threshold)
                rhs.confidence = -1.0f;
        }
    }
}

void YoloV8DecodeNode::NMS(std::vector<std::tuple<int, float, int, bool>>& conf_list, float* bottom,
                           int num_features, int num_boxes, bool is_format1) {
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    cosmo::nn::axera::diagnostics::RecordCpuNms();
#endif
    std::sort(conf_list.begin(), conf_list.end(),
              [](const std::tuple<int, float, int, bool>& a, const std::tuple<int, float, int, bool>& b) {
                  return std::get<1>(a) > std::get<1>(b);
              });

    for (size_t i = 0; i < conf_list.size(); i++) {
        if (!std::get<3>(conf_list.at(i)))
            continue;

        int class_id = std::get<2>(conf_list.at(i));
        for (size_t j = i + 1; j < conf_list.size(); j++) {
            if (!std::get<3>(conf_list.at(j)))
                continue;

            // Only apply NMS within the same class
            if (class_id != std::get<2>(conf_list.at(j)))
                continue;

            // Get box coordinates
            float box1[4], box2[4];
            int idx1 = std::get<0>(conf_list.at(i));
            int idx2 = std::get<0>(conf_list.at(j));

            if (is_format1) {
                // Format 1: [batch, num_boxes, num_features]
                float* box1_ptr = bottom + idx1 * num_features;
                float* box2_ptr = bottom + idx2 * num_features;
                box1[0]         = box1_ptr[0];
                box1[1]         = box1_ptr[1];
                box1[2]         = box1_ptr[2];
                box1[3]         = box1_ptr[3];
                box2[0]         = box2_ptr[0];
                box2[1]         = box2_ptr[1];
                box2[2]         = box2_ptr[2];
                box2[3]         = box2_ptr[3];
            } else {
                // Format 2: [batch, num_features, num_boxes]
                box1[0] = bottom[0 * num_boxes + idx1];
                box1[1] = bottom[1 * num_boxes + idx1];
                box1[2] = bottom[2 * num_boxes + idx1];
                box1[3] = bottom[3 * num_boxes + idx1];
                box2[0] = bottom[0 * num_boxes + idx2];
                box2[1] = bottom[1 * num_boxes + idx2];
                box2[2] = bottom[2 * num_boxes + idx2];
                box2[3] = bottom[3 * num_boxes + idx2];
            }

            float iou = IOU(box1, box2);
            if (iou >= nms_threshold) {
                std::get<3>(conf_list.at(j)) = false;
            }
        }
    }
}

float YoloV8DecodeNode::IOU(float* box1, float* box2) {
    // YOLOv8 output format: x_center, y_center, width, height
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

void YoloV8DecodeNode::ResetTopBlob(std::shared_ptr<Blob> top_blob) {
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
