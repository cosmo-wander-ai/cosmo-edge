#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/device/rknn/rknn_yolov8_adapter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

#include "nn/device/rknn/rknn_yolo26_adapter.h"

namespace cosmo::nn {
namespace {

    constexpr std::array<RknnOutputAdapterRegistryEntry, 8> kOutputAdapterRegistry{{
        {RknnOutputAdapterKind::GenericTensorV1, "generic_tensor_v1", true, true},
        {RknnOutputAdapterKind::YoloAnchor3HeadV1, "yolo_anchor_3head_v1", false, false},
        {RknnOutputAdapterKind::YoloDfl6HeadV1, "yolo_dfl_6head_v1", true, true},
        {RknnOutputAdapterKind::YoloDfl9HeadScoreSumV1, "yolo_dfl_9head_score_sum_v1", true, true},
        {RknnOutputAdapterKind::Yolo26OneToOne6HeadV1, "yolo26_one2one_6head_v1", true, true},
        {RknnOutputAdapterKind::YoloPoseV1, "yolo_pose_v1", false, false},
        {RknnOutputAdapterKind::YoloSegV1, "yolo_seg_v1", false, false},
        {RknnOutputAdapterKind::YoloObbV1, "yolo_obb_v1", false, false},
    }};

    bool ParseNchw(const std::vector<int>& shape, int& channels, int& height, int& width) {
        if (shape.size() != 4 || shape[0] != 1 || shape[1] <= 0 || shape[2] <= 0 || shape[3] <= 0)
            return false;
        channels = shape[1];
        height   = shape[2];
        width    = shape[3];
        return true;
    }

    size_t ShapeCount(const std::vector<int>& shape) {
        size_t count = 1;
        for (int dim : shape) {
            if (dim <= 0 || count > std::numeric_limits<size_t>::max() / static_cast<size_t>(dim))
                return 0;
            count *= static_cast<size_t>(dim);
        }
        return count;
    }

    float Sigmoid(float value) {
        if (value >= 0.0f)
            return 1.0f / (1.0f + std::exp(-value));
        const float exponential = std::exp(value);
        return exponential / (1.0f + exponential);
    }

    float ClassScore(const RknnOutputAdapterContract& contract, float value) {
        return contract.class_scores_are_probabilities ? std::clamp(value, 0.0f, 1.0f) : Sigmoid(value);
    }

    uint64_t ElapsedNanoseconds(std::chrono::steady_clock::time_point started_at) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - started_at)
                                         .count());
    }

    size_t QuantizedIndex(int8_t value) {
        return static_cast<size_t>(static_cast<int>(value) + 128);
    }

    bool DetectYolov8DflContract(const std::vector<std::vector<int>>& shapes, size_t outputs_per_branch,
                                 RknnOutputAdapterContract& contract, std::string& error) {
        if (shapes.size() != outputs_per_branch * 3)
            return false;

        const bool has_score_sum = outputs_per_branch == 3;
        int class_count          = 0;
        int point_count          = 0;
        int previous_height      = std::numeric_limits<int>::max();
        for (size_t branch_index = 0; branch_index < 3; ++branch_index) {
            const size_t base = branch_index * outputs_per_branch;
            int box_channels = 0, box_height = 0, box_width = 0;
            int cls_channels = 0, cls_height = 0, cls_width = 0;
            if (!ParseNchw(shapes[base], box_channels, box_height, box_width) ||
                !ParseNchw(shapes[base + 1], cls_channels, cls_height, cls_width)) {
                error = "YOLOv8 RKNN heads must be static NCHW tensors with batch 1";
                return false;
            }
            if (box_channels != 64 || box_height != cls_height || box_width != cls_width) {
                error = "YOLOv8 RKNN box/class head dimensions do not match";
                return false;
            }
            if (cls_channels <= 0 || (class_count != 0 && cls_channels != class_count)) {
                error = "YOLOv8 RKNN class counts are inconsistent";
                return false;
            }
            if (has_score_sum) {
                int sum_channels = 0, sum_height = 0, sum_width = 0;
                if (!ParseNchw(shapes[base + 2], sum_channels, sum_height, sum_width) || sum_channels != 1 ||
                    sum_height != box_height || sum_width != box_width) {
                    error = "YOLOv8 RKNN score-sum head dimensions do not match";
                    return false;
                }
            }
            if (box_height >= previous_height) {
                error = "YOLOv8 RKNN heads must be ordered from fine to coarse stride";
                return false;
            }
            if (box_height > std::numeric_limits<int>::max() / box_width ||
                point_count > std::numeric_limits<int>::max() - box_height * box_width) {
                error = "YOLOv8 RKNN point count overflows";
                return false;
            }

            auto& branch           = contract.branches[branch_index];
            branch.box_index       = base;
            branch.class_index     = base + 1;
            branch.score_sum_index = has_score_sum ? base + 2 : RknnYolov8BranchContract::kNoTensor;
            branch.height          = box_height;
            branch.width           = box_width;
            class_count            = cls_channels;
            point_count += box_height * box_width;
            previous_height = box_height;
        }

        contract.kind          = has_score_sum ? RknnOutputAdapterKind::YoloDfl9HeadScoreSumV1
                                               : RknnOutputAdapterKind::YoloDfl6HeadV1;
        contract.class_count   = class_count;
        contract.point_count   = point_count;
        contract.logical_shape = {1, 4 + class_count, point_count};
        contract.class_scores_are_probabilities = has_score_sum;
        error.clear();
        return true;
    }

}  // namespace

const std::array<RknnOutputAdapterRegistryEntry, 8>& RknnOutputAdapterRegistry() {
    return kOutputAdapterRegistry;
}

const char* RknnOutputAdapterName(RknnOutputAdapterKind kind) {
    const auto found =
        std::find_if(kOutputAdapterRegistry.begin(), kOutputAdapterRegistry.end(),
                     [kind](const RknnOutputAdapterRegistryEntry& entry) { return entry.kind == kind; });
    return found == kOutputAdapterRegistry.end() ? "unknown" : found->name;
}

bool IsRknnYolov8DflAdapter(RknnOutputAdapterKind kind) {
    return kind == RknnOutputAdapterKind::YoloDfl6HeadV1 ||
           kind == RknnOutputAdapterKind::YoloDfl9HeadScoreSumV1;
}

bool IsRknnYolo26OneToOneAdapter(RknnOutputAdapterKind kind) {
    return kind == RknnOutputAdapterKind::Yolo26OneToOne6HeadV1;
}

bool ResolveRknnOutputAdapter(const std::vector<std::vector<int>>& shapes,
                              RknnOutputAdapterContract& contract, std::string& error) {
    contract = {};
    std::string ignored_error;
    if ((shapes.size() == 6 && DetectYolov8DflContract(shapes, 2, contract, ignored_error)) ||
        (shapes.size() == 9 && DetectYolov8DflContract(shapes, 3, contract, ignored_error))) {
        error.clear();
        return true;
    }
    RknnYolo26Layout yolo26;
    if (DetectRknnYolo26Layout(shapes, yolo26, ignored_error)) {
        contract               = {};
        contract.kind          = RknnOutputAdapterKind::Yolo26OneToOne6HeadV1;
        contract.class_count   = yolo26.class_count;
        contract.point_count   = yolo26.point_count;
        contract.logical_shape = yolo26.logical_shape;
        error.clear();
        return true;
    }
    contract      = {};
    contract.kind = RknnOutputAdapterKind::GenericTensorV1;
    error.clear();
    return true;
}

bool DetectRknnYolov8Layout(const std::vector<std::vector<int>>& shapes, RknnYolov8Layout& layout,
                            std::string& error) {
    layout = {};
    if (shapes.size() == 6)
        return DetectYolov8DflContract(shapes, 2, layout, error);
    if (shapes.size() == 9)
        return DetectYolov8DflContract(shapes, 3, layout, error);
    error = "YOLOv8 RKNN adapter requires six or nine ordered DFL outputs";
    return false;
}

bool ReconstructRknnYolov8(const std::vector<RknnYolov8Head>& heads, int input_height, int input_width,
                           float* output, size_t output_count, std::string& error) {
    std::vector<std::vector<int>> shapes;
    shapes.reserve(heads.size());
    for (const auto& head : heads)
        shapes.push_back(head.shape);

    RknnYolov8Layout layout;
    if (!DetectRknnYolov8Layout(shapes, layout, error))
        return false;
    if (!output || input_height <= 0 || input_width <= 0) {
        error = "YOLOv8 RKNN adapter received an invalid output or input size";
        return false;
    }
    const size_t required =
        static_cast<size_t>(4 + layout.class_count) * static_cast<size_t>(layout.point_count);
    if (output_count < required) {
        error = "YOLOv8 RKNN logical output buffer is too small";
        return false;
    }

    int point_offset = 0;
    for (const auto& branch : layout.branches) {
        const auto& box_head = heads[branch.box_index];
        const auto& cls_head = heads[branch.class_index];
        int box_channels = 0, height = 0, width = 0;
        int cls_channels = 0, cls_height = 0, cls_width = 0;
        ParseNchw(box_head.shape, box_channels, height, width);
        ParseNchw(cls_head.shape, cls_channels, cls_height, cls_width);
        if (!box_head.data || !cls_head.data || box_head.element_count != ShapeCount(box_head.shape) ||
            cls_head.element_count != ShapeCount(cls_head.shape)) {
            error = "YOLOv8 RKNN output byte count does not match the queried head shape";
            return false;
        }

        const int spatial_count = height * width;
        const float stride_x    = static_cast<float>(input_width) / static_cast<float>(width);
        const float stride_y    = static_cast<float>(input_height) / static_cast<float>(height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int spatial_index = y * width + x;
                float distance[4]{};
                for (int side = 0; side < 4; ++side) {
                    float maximum = -std::numeric_limits<float>::infinity();
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        maximum = std::max(maximum, box_head.data[channel * spatial_count + spatial_index]);
                    }
                    float denominator = 0.0f;
                    float numerator   = 0.0f;
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        const float value =
                            std::exp(box_head.data[channel * spatial_count + spatial_index] - maximum);
                        denominator += value;
                        numerator += value * static_cast<float>(bin);
                    }
                    if (!(denominator > 0.0f) || !std::isfinite(denominator)) {
                        error = "YOLOv8 RKNN DFL softmax produced an invalid denominator";
                        return false;
                    }
                    distance[side] = numerator / denominator;
                }

                const float left        = (static_cast<float>(x) + 0.5f - distance[0]) * stride_x;
                const float top         = (static_cast<float>(y) + 0.5f - distance[1]) * stride_y;
                const float right       = (static_cast<float>(x) + 0.5f + distance[2]) * stride_x;
                const float bottom      = (static_cast<float>(y) + 0.5f + distance[3]) * stride_y;
                const int logical_index = point_offset + spatial_index;
                output[logical_index]   = (left + right) * 0.5f;
                output[layout.point_count + logical_index]     = (top + bottom) * 0.5f;
                output[2 * layout.point_count + logical_index] = right - left;
                output[3 * layout.point_count + logical_index] = bottom - top;
                for (int cls = 0; cls < layout.class_count; ++cls) {
                    output[(4 + cls) * layout.point_count + logical_index] =
                        ClassScore(layout, cls_head.data[cls * spatial_count + spatial_index]);
                }
            }
        }
        point_offset += spatial_count;
    }
    return true;
}

bool ReconstructRknnYolov8Quantized(const std::vector<RknnYolov8QuantizedHead>& heads, int input_height,
                                    int input_width, float* output, size_t output_count, std::string& error,
                                    RknnYolov8TransformTiming* timing) {
    std::vector<std::vector<int>> shapes;
    shapes.reserve(heads.size());
    for (const auto& head : heads)
        shapes.push_back(head.shape);

    RknnYolov8Layout layout;
    if (!DetectRknnYolov8Layout(shapes, layout, error))
        return false;
    if (!output || input_height <= 0 || input_width <= 0) {
        error = "YOLOv8 RKNN quantized adapter received an invalid output or input size";
        return false;
    }
    const size_t required =
        static_cast<size_t>(4 + layout.class_count) * static_cast<size_t>(layout.point_count);
    if (output_count < required) {
        error = "YOLOv8 RKNN logical output buffer is too small";
        return false;
    }
    if (timing)
        *timing = {};

    int point_offset = 0;
    for (const auto& branch : layout.branches) {
        const auto& box_head = heads[branch.box_index];
        const auto& cls_head = heads[branch.class_index];
        int box_channels = 0, height = 0, width = 0;
        int cls_channels = 0, cls_height = 0, cls_width = 0;
        ParseNchw(box_head.shape, box_channels, height, width);
        ParseNchw(cls_head.shape, cls_channels, cls_height, cls_width);
        if (!box_head.data || !cls_head.data || box_head.element_count != ShapeCount(box_head.shape) ||
            cls_head.element_count != ShapeCount(cls_head.shape)) {
            error = "YOLOv8 RKNN quantized output byte count does not match the queried head shape";
            return false;
        }
        if (!std::isfinite(box_head.scale) || !(box_head.scale > 0.0f) || !std::isfinite(cls_head.scale) ||
            !(cls_head.scale > 0.0f) || box_head.zero_point < -128 || box_head.zero_point > 127 ||
            cls_head.zero_point < -128 || cls_head.zero_point > 127) {
            error = "YOLOv8 RKNN quantized output parameters are invalid";
            return false;
        }

        std::array<float, 256> dfl_exp_lut{};
        std::array<float, 256> class_score_lut{};
        for (size_t index = 0; index < dfl_exp_lut.size(); ++index) {
            dfl_exp_lut[index]  = std::exp(-static_cast<float>(index) * box_head.scale);
            const int quantized = static_cast<int>(index) - 128;
            class_score_lut[index] =
                ClassScore(layout, (static_cast<float>(quantized) - cls_head.zero_point) * cls_head.scale);
        }

        const int spatial_count = height * width;
        const float stride_x    = static_cast<float>(input_width) / static_cast<float>(width);
        const float stride_y    = static_cast<float>(input_height) / static_cast<float>(height);
        const auto dfl_started  = std::chrono::steady_clock::now();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int spatial_index = y * width + x;
                float distance[4]{};
                for (int side = 0; side < 4; ++side) {
                    int maximum = -128;
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        maximum           = std::max(
                            maximum,
                            static_cast<int>(box_head.data[channel * spatial_count + spatial_index]));
                    }
                    float denominator = 0.0f;
                    float numerator   = 0.0f;
                    for (int bin = 0; bin < 16; ++bin) {
                        const int channel = side * 16 + bin;
                        const int quantized =
                            static_cast<int>(box_head.data[channel * spatial_count + spatial_index]);
                        const float value = dfl_exp_lut[static_cast<size_t>(maximum - quantized)];
                        denominator += value;
                        numerator += value * static_cast<float>(bin);
                    }
                    distance[side] = numerator / denominator;
                }

                const float left        = (static_cast<float>(x) + 0.5f - distance[0]) * stride_x;
                const float top         = (static_cast<float>(y) + 0.5f - distance[1]) * stride_y;
                const float right       = (static_cast<float>(x) + 0.5f + distance[2]) * stride_x;
                const float bottom      = (static_cast<float>(y) + 0.5f + distance[3]) * stride_y;
                const int logical_index = point_offset + spatial_index;
                output[logical_index]   = (left + right) * 0.5f;
                output[layout.point_count + logical_index]     = (top + bottom) * 0.5f;
                output[2 * layout.point_count + logical_index] = right - left;
                output[3 * layout.point_count + logical_index] = bottom - top;
            }
        }
        if (timing)
            timing->dfl_nanoseconds += ElapsedNanoseconds(dfl_started);

        const auto class_started = std::chrono::steady_clock::now();
        for (int cls = 0; cls < layout.class_count; ++cls) {
            const auto* source = cls_head.data + cls * spatial_count;
            auto* destination  = output + (4 + cls) * layout.point_count + point_offset;
            for (int spatial_index = 0; spatial_index < spatial_count; ++spatial_index)
                destination[spatial_index] = class_score_lut[QuantizedIndex(source[spatial_index])];
        }
        if (timing)
            timing->class_nanoseconds += ElapsedNanoseconds(class_started);
        point_offset += spatial_count;
    }
    return true;
}

bool DecodeRknnYolov8QuantizedCandidates(const std::vector<RknnYolov8QuantizedHead>& heads, int input_height,
                                         int input_width, float confidence_threshold,
                                         RknnYolov8CandidateScratch& scratch,
                                         std::vector<Yolov8Candidate>& candidates, std::string& error,
                                         RknnYolov8CandidateTiming* timing) {
    candidates.clear();
    if (timing)
        *timing = {};

    std::vector<std::vector<int>> shapes;
    shapes.reserve(heads.size());
    for (const auto& head : heads)
        shapes.push_back(head.shape);

    RknnYolov8Layout layout;
    if (!DetectRknnYolov8Layout(shapes, layout, error))
        return false;
    if (input_height <= 0 || input_width <= 0 || !std::isfinite(confidence_threshold)) {
        error = "RKNN YOLOv8 candidate adapter received an invalid input contract";
        return false;
    }
    const auto initial_capacity = static_cast<size_t>(std::min(layout.point_count, 1024));
    if (candidates.capacity() < initial_capacity)
        candidates.reserve(initial_capacity);

    for (const auto& branch : layout.branches) {
        const auto& box_head = heads[branch.box_index];
        const auto& cls_head = heads[branch.class_index];
        const RknnYolov8QuantizedHead* score_sum_head =
            branch.HasScoreSum() ? &heads[branch.score_sum_index] : nullptr;
        int box_channels = 0, height = 0, width = 0;
        int cls_channels = 0, cls_height = 0, cls_width = 0;
        ParseNchw(box_head.shape, box_channels, height, width);
        ParseNchw(cls_head.shape, cls_channels, cls_height, cls_width);
        if (!box_head.data || !cls_head.data || box_head.element_count != ShapeCount(box_head.shape) ||
            cls_head.element_count != ShapeCount(cls_head.shape)) {
            error = "RKNN YOLOv8 candidate head byte count does not match its shape";
            candidates.clear();
            return false;
        }
        if (score_sum_head &&
            (!score_sum_head->data || score_sum_head->element_count != ShapeCount(score_sum_head->shape) ||
             !std::isfinite(score_sum_head->scale) || !(score_sum_head->scale > 0.0f) ||
             score_sum_head->zero_point < -128 || score_sum_head->zero_point > 127)) {
            error = "RKNN YOLOv8 score-sum output parameters are invalid";
            candidates.clear();
            return false;
        }
        if (!std::isfinite(box_head.scale) || !(box_head.scale > 0.0f) || !std::isfinite(cls_head.scale) ||
            !(cls_head.scale > 0.0f) || box_head.zero_point < -128 || box_head.zero_point > 127 ||
            cls_head.zero_point < -128 || cls_head.zero_point > 127) {
            error = "RKNN YOLOv8 candidate quantization parameters are invalid";
            candidates.clear();
            return false;
        }

        std::array<float, 256> dfl_exp_lut{};
        std::array<float, 256> class_score_lut{};
        for (size_t index = 0; index < dfl_exp_lut.size(); ++index) {
            dfl_exp_lut[index]  = std::exp(-static_cast<float>(index) * box_head.scale);
            const int quantized = static_cast<int>(index) - 128;
            class_score_lut[index] =
                ClassScore(layout, (static_cast<float>(quantized) - cls_head.zero_point) * cls_head.scale);
        }

        const int spatial_count  = height * width;
        const auto class_started = std::chrono::steady_clock::now();
        scratch.class_max.assign(static_cast<size_t>(spatial_count), std::numeric_limits<int8_t>::min());
        scratch.class_ids.assign(static_cast<size_t>(spatial_count), -1);
        scratch.active_points.clear();
        if (score_sum_head) {
            if (scratch.active_points.capacity() < static_cast<size_t>(spatial_count))
                scratch.active_points.reserve(static_cast<size_t>(spatial_count));
            for (int spatial_index = 0; spatial_index < spatial_count; ++spatial_index) {
                const float score_sum =
                    (static_cast<float>(score_sum_head->data[spatial_index]) - score_sum_head->zero_point) *
                    score_sum_head->scale;
                if (score_sum >= confidence_threshold) {
                    scratch.active_points.push_back(spatial_index);
                } else if (timing) {
                    ++timing->score_sum_points_rejected;
                }
            }
        }
        for (int cls = 0; cls < cls_channels; ++cls) {
            const auto* source    = cls_head.data + cls * spatial_count;
            const auto update_max = [&](int spatial_index) {
                if (scratch.class_ids[spatial_index] < 0 ||
                    source[spatial_index] > scratch.class_max[spatial_index]) {
                    scratch.class_max[spatial_index] = source[spatial_index];
                    scratch.class_ids[spatial_index] = cls;
                }
            };
            if (score_sum_head) {
                for (int spatial_index : scratch.active_points)
                    update_max(spatial_index);
            } else {
                for (int spatial_index = 0; spatial_index < spatial_count; ++spatial_index)
                    update_max(spatial_index);
            }
        }
        if (timing) {
            timing->class_nanoseconds += ElapsedNanoseconds(class_started);
            timing->points_scanned += static_cast<uint64_t>(spatial_count);
        }

        const float stride_x   = static_cast<float>(input_width) / static_cast<float>(width);
        const float stride_y   = static_cast<float>(input_height) / static_cast<float>(height);
        const auto dfl_started = std::chrono::steady_clock::now();
        for (int spatial_index = 0; spatial_index < spatial_count; ++spatial_index) {
            if (scratch.class_ids[spatial_index] < 0)
                continue;
            const float confidence = class_score_lut[QuantizedIndex(scratch.class_max[spatial_index])];
            if (confidence < confidence_threshold)
                continue;

            float distance[4]{};
            for (int side = 0; side < 4; ++side) {
                int maximum = -128;
                for (int bin = 0; bin < 16; ++bin) {
                    const int channel = side * 16 + bin;
                    maximum           = std::max(
                        maximum, static_cast<int>(box_head.data[channel * spatial_count + spatial_index]));
                }
                float denominator = 0.0f;
                float numerator   = 0.0f;
                for (int bin = 0; bin < 16; ++bin) {
                    const int channel = side * 16 + bin;
                    const int quantized =
                        static_cast<int>(box_head.data[channel * spatial_count + spatial_index]);
                    const float value = dfl_exp_lut[static_cast<size_t>(maximum - quantized)];
                    denominator += value;
                    numerator += value * static_cast<float>(bin);
                }
                if (!(denominator > 0.0f) || !std::isfinite(denominator)) {
                    error = "RKNN YOLOv8 candidate DFL produced an invalid denominator";
                    candidates.clear();
                    return false;
                }
                distance[side] = numerator / denominator;
            }

            const int x        = spatial_index % width;
            const int y        = spatial_index / width;
            const float left   = (static_cast<float>(x) + 0.5f - distance[0]) * stride_x;
            const float top    = (static_cast<float>(y) + 0.5f - distance[1]) * stride_y;
            const float right  = (static_cast<float>(x) + 0.5f + distance[2]) * stride_x;
            const float bottom = (static_cast<float>(y) + 0.5f + distance[3]) * stride_y;
            candidates.push_back({(left + right) * 0.5f, (top + bottom) * 0.5f, right - left, bottom - top,
                                  confidence, scratch.class_ids[spatial_index]});
            if (timing)
                ++timing->points_decoded;
        }
        if (timing)
            timing->dfl_nanoseconds += ElapsedNanoseconds(dfl_started);
    }
    return true;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
