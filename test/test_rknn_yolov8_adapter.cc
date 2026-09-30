#include "catch_amalgamated.hpp"

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "nn/core/blob.h"
#include "nn/core/shared_resource.h"
#include "nn/device/rknn/rknn_yolov8_adapter.h"
#include "nn/node/yolov8_decode_node.h"
#include "nn/utils/op.h"

TEST_CASE("RKNN YOLOv8 adapter reconstructs the logical tensor", "[nn][rknn][yolov8]") {
    using namespace cosmo::nn;

    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 2, 2}, {1, 3, 2, 2}, {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    RknnYolov8Layout layout;
    std::string error;
    REQUIRE(DetectRknnYolov8Layout(shapes, layout, error));
    REQUIRE(layout.logical_shape == std::vector<int>{1, 7, 21});

    std::vector<std::vector<float>> values;
    std::vector<RknnYolov8Head> heads;
    for (const auto& shape : shapes) {
        const size_t count = static_cast<size_t>(shape[1] * shape[2] * shape[3]);
        values.emplace_back(count, 0.0f);
        heads.push_back({values.back().data(), values.back().size(), shape});
    }

    std::vector<float> output(static_cast<size_t>(7 * 21));
    REQUIRE(ReconstructRknnYolov8(heads, 32, 32, output.data(), output.size(), error));
    CHECK(output[0] == Catch::Approx(4.0f));
    CHECK(output[21] == Catch::Approx(4.0f));
    CHECK(output[42] == Catch::Approx(120.0f));
    CHECK(output[63] == Catch::Approx(120.0f));
    CHECK(output[84] == Catch::Approx(0.5f));
    CHECK(output[105] == Catch::Approx(0.5f));
    CHECK(output[126] == Catch::Approx(0.5f));
    CHECK(output[16] == Catch::Approx(8.0f));
    CHECK(output[20] == Catch::Approx(16.0f));
}

TEST_CASE("RKNN YOLOv8 adapter rejects malformed head order", "[nn][rknn][yolov8]") {
    using namespace cosmo::nn;
    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    RknnYolov8Layout layout;
    std::string error;
    CHECK_FALSE(DetectRknnYolov8Layout(shapes, layout, error));
    CHECK_FALSE(error.empty());

    RknnOutputAdapterContract fallback;
    REQUIRE(ResolveRknnOutputAdapter(shapes, fallback, error));
    CHECK(fallback.kind == RknnOutputAdapterKind::GenericTensorV1);
    CHECK(fallback.logical_shape.empty());
}

TEST_CASE("RKNN output adapter registry separates implemented tensor contracts",
          "[nn][rknn][output-adapter]") {
    using namespace cosmo::nn;

    const auto& registry = RknnOutputAdapterRegistry();
    REQUIRE(registry.size() == 8);
    CHECK(std::string(RknnOutputAdapterName(RknnOutputAdapterKind::GenericTensorV1)) == "generic_tensor_v1");
    CHECK(std::string(RknnOutputAdapterName(RknnOutputAdapterKind::YoloDfl6HeadV1)) == "yolo_dfl_6head_v1");
    CHECK(std::string(RknnOutputAdapterName(RknnOutputAdapterKind::YoloDfl9HeadScoreSumV1)) ==
          "yolo_dfl_9head_score_sum_v1");

    const auto pose =
        std::find_if(registry.begin(), registry.end(), [](const RknnOutputAdapterRegistryEntry& entry) {
            return entry.kind == RknnOutputAdapterKind::YoloPoseV1;
        });
    REQUIRE(pose != registry.end());
    CHECK_FALSE(pose->implemented);
    CHECK_FALSE(pose->auto_detect);

    const std::vector<std::vector<int>> generic_shapes{{1, 2}, {1, 3}};
    RknnOutputAdapterContract contract;
    std::string error;
    REQUIRE(ResolveRknnOutputAdapter(generic_shapes, contract, error));
    CHECK(contract.kind == RknnOutputAdapterKind::GenericTensorV1);
}

TEST_CASE("RKNN YOLOv8 nine-head contract preserves probability scores for FP16 fallback",
          "[nn][rknn][yolov8][score-sum][fp16]") {
    using namespace cosmo::nn;

    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4},  {1, 1, 4, 4}, {1, 64, 2, 2}, {1, 3, 2, 2},
        {1, 1, 2, 2},  {1, 64, 1, 1}, {1, 3, 1, 1}, {1, 1, 1, 1},
    };
    RknnYolov8Layout layout;
    std::string error;
    REQUIRE(DetectRknnYolov8Layout(shapes, layout, error));
    CHECK(layout.kind == RknnOutputAdapterKind::YoloDfl9HeadScoreSumV1);
    CHECK(layout.class_scores_are_probabilities);
    CHECK(layout.branches[0].box_index == 0);
    CHECK(layout.branches[0].class_index == 1);
    CHECK(layout.branches[0].score_sum_index == 2);
    CHECK(layout.logical_shape == std::vector<int>{1, 7, 21});

    std::vector<std::vector<float>> values;
    std::vector<RknnYolov8Head> heads;
    values.reserve(shapes.size());
    heads.reserve(shapes.size());
    for (size_t index = 0; index < shapes.size(); ++index) {
        const auto& shape     = shapes[index];
        const size_t count    = static_cast<size_t>(shape[1] * shape[2] * shape[3]);
        const bool class_head = index % 3 == 1;
        values.emplace_back(count, class_head ? 0.2f : 0.0f);
        heads.push_back({values.back().data(), values.back().size(), shape});
    }

    std::vector<float> output(static_cast<size_t>(7 * 21));
    REQUIRE(ReconstructRknnYolov8(heads, 32, 32, output.data(), output.size(), error));
    CHECK(output[4 * 21] == Catch::Approx(0.2f));
    CHECK(output[5 * 21] == Catch::Approx(0.2f));
    CHECK(output[6 * 21] == Catch::Approx(0.2f));

    auto malformed = shapes;
    malformed[2]   = {1, 2, 4, 4};
    CHECK_FALSE(DetectRknnYolov8Layout(malformed, layout, error));
    CHECK(error.find("score-sum") != std::string::npos);
}

TEST_CASE("RKNN YOLOv8 nine-head direct path uses score-sum without double sigmoid",
          "[nn][rknn][yolov8][score-sum][direct-candidates]") {
    using namespace cosmo::nn;

    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4},  {1, 1, 4, 4}, {1, 64, 2, 2}, {1, 3, 2, 2},
        {1, 1, 2, 2},  {1, 64, 1, 1}, {1, 3, 1, 1}, {1, 1, 1, 1},
    };
    std::vector<std::vector<int8_t>> values;
    std::vector<RknnYolov8QuantizedHead> heads;
    values.reserve(shapes.size());
    heads.reserve(shapes.size());
    for (size_t index = 0; index < shapes.size(); ++index) {
        const auto& shape     = shapes[index];
        const size_t count    = static_cast<size_t>(shape[1] * shape[2] * shape[3]);
        const bool class_head = index % 3 == 1;
        const bool sum_head   = index % 3 == 2;
        values.emplace_back(count, class_head
                                       ? static_cast<int8_t>(5)
                                       : (sum_head ? static_cast<int8_t>(10) : static_cast<int8_t>(0)));
        heads.push_back({values.back().data(), count, shape, 0, class_head || sum_head ? 0.01f : 0.1f});
    }
    values[1][0] = 80;
    values[2][0] = 90;

    RknnYolov8CandidateScratch scratch;
    RknnYolov8CandidateTiming timing;
    std::vector<Yolov8Candidate> candidates;
    std::string error;
    REQUIRE(DecodeRknnYolov8QuantizedCandidates(heads, 32, 32, 0.25f, scratch, candidates, error, &timing));
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().confidence == Catch::Approx(0.8f));
    CHECK(candidates.front().class_id == 0);
    CHECK(timing.points_scanned == 21);
    CHECK(timing.points_decoded == 1);
    CHECK(timing.score_sum_points_rejected == 20);

    std::vector<float> reconstructed(static_cast<size_t>(7 * 21));
    RknnYolov8TransformTiming transform_timing;
    REQUIRE(ReconstructRknnYolov8Quantized(heads, 32, 32, reconstructed.data(), reconstructed.size(), error,
                                           &transform_timing));
    CHECK(reconstructed[4 * 21] == Catch::Approx(0.8f));
}

TEST_CASE("RKNN YOLOv8 quantized adapter matches dequantized float heads",
          "[nn][rknn][yolov8][fast-output]") {
    using namespace cosmo::nn;

    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 2, 2}, {1, 3, 2, 2}, {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    std::vector<std::vector<int8_t>> quantized_values;
    std::vector<std::vector<float>> float_values;
    std::vector<RknnYolov8QuantizedHead> quantized_heads;
    std::vector<RknnYolov8Head> float_heads;
    for (size_t head = 0; head < shapes.size(); ++head) {
        const size_t count       = static_cast<size_t>(shapes[head][1] * shapes[head][2] * shapes[head][3]);
        const int32_t zero_point = head % 2 == 0 ? -61 : 114;
        const float scale        = head % 2 == 0 ? 0.11488f : 0.113557f;
        quantized_values.emplace_back(count);
        float_values.emplace_back(count);
        for (size_t index = 0; index < count; ++index) {
            const auto value               = static_cast<int8_t>(static_cast<int>(index % 97) - 48);
            quantized_values.back()[index] = value;
            float_values.back()[index] = (static_cast<float>(value) - static_cast<float>(zero_point)) * scale;
        }
        quantized_heads.push_back({quantized_values.back().data(), count, shapes[head], zero_point, scale});
        float_heads.push_back({float_values.back().data(), count, shapes[head]});
    }

    std::vector<float> expected(static_cast<size_t>(7 * 21));
    std::vector<float> actual(expected.size());
    std::string error;
    REQUIRE(ReconstructRknnYolov8(float_heads, 32, 32, expected.data(), expected.size(), error));
    RknnYolov8TransformTiming timing;
    REQUIRE(ReconstructRknnYolov8Quantized(quantized_heads, 32, 32, actual.data(), actual.size(), error,
                                           &timing));
    for (size_t index = 0; index < expected.size(); ++index)
        CHECK(actual[index] == Catch::Approx(expected[index]).margin(1e-4f));
}

TEST_CASE("RKNN YOLOv8 quantized adapter rejects invalid quantization", "[nn][rknn][yolov8][fast-output]") {
    using namespace cosmo::nn;
    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 2, 2}, {1, 3, 2, 2}, {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    std::vector<std::vector<int8_t>> values;
    std::vector<RknnYolov8QuantizedHead> heads;
    for (const auto& shape : shapes) {
        const size_t count = static_cast<size_t>(shape[1] * shape[2] * shape[3]);
        values.emplace_back(count, 0);
        heads.push_back({values.back().data(), count, shape, 0, 0.1f});
    }
    heads[1].scale = 0.0f;
    std::vector<float> output(static_cast<size_t>(7 * 21));
    std::string error;
    CHECK_FALSE(ReconstructRknnYolov8Quantized(heads, 32, 32, output.data(), output.size(), error));
    CHECK_FALSE(error.empty());

    RknnYolov8CandidateScratch scratch;
    std::vector<Yolov8Candidate> candidates;
    error.clear();
    CHECK_FALSE(DecodeRknnYolov8QuantizedCandidates(heads, 32, 32, 0.25f, scratch, candidates, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("RKNN YOLOv8 direct candidates match the logical tensor decoder",
          "[nn][rknn][yolov8][direct-candidates]") {
    using namespace cosmo::nn;

    const std::vector<std::vector<int>> shapes{
        {1, 64, 4, 4}, {1, 3, 4, 4}, {1, 64, 2, 2}, {1, 3, 2, 2}, {1, 64, 1, 1}, {1, 3, 1, 1},
    };
    std::vector<std::vector<int8_t>> quantized_values;
    std::vector<std::vector<float>> float_values;
    std::vector<RknnYolov8QuantizedHead> quantized_heads;
    std::vector<RknnYolov8Head> float_heads;
    for (size_t head = 0; head < shapes.size(); ++head) {
        const size_t count    = static_cast<size_t>(shapes[head][1] * shapes[head][2] * shapes[head][3]);
        const bool class_head = head % 2 == 1;
        const int8_t initial  = class_head ? static_cast<int8_t>(-30) : static_cast<int8_t>(0);
        quantized_values.emplace_back(count, initial);
        float_values.emplace_back(count, static_cast<float>(initial) * 0.1f);
        quantized_heads.push_back({quantized_values.back().data(), count, shapes[head], 0, 0.1f});
        float_heads.push_back({float_values.back().data(), count, shapes[head]});
    }

    const auto set_score = [&](size_t head, int class_id, int spatial_index, int8_t value) {
        const int spatial_count       = shapes[head][2] * shapes[head][3];
        const size_t index            = static_cast<size_t>(class_id * spatial_count + spatial_index);
        quantized_values[head][index] = value;
        float_values[head][index]     = static_cast<float>(value) * 0.1f;
    };
    set_score(1, 0, 0, 20);
    set_score(1, 1, 15, 18);
    set_score(3, 2, 0, 22);

    std::vector<float> logical_output(static_cast<size_t>(7 * 21));
    std::string error;
    REQUIRE(ReconstructRknnYolov8(float_heads, 32, 32, logical_output.data(), logical_output.size(), error));

    SharedResource resource;
    YoloV8DecodeNode node;
    node.SetSharedResource(&resource);
    node.SetMaxBatch(1);
    YoloPost post;
    post.nms_threshold      = 0.7f;
    post.nms_detection_conf = 0.25f;
    post.top_k              = 8;
    post.input_width        = 32;
    post.input_height       = 32;
    node.LoadParam(&post);
    REQUIRE(bool(node.InferTopShapes()));

    BlobDesc input_desc;
    input_desc.device_type = DEVICE_NAIVE;
    input_desc.data_type   = DATA_TYPE_FLOAT;
    input_desc.data_format = DATA_FORMAT_NCHW;
    input_desc.dims        = {1, 7, 21};
    auto input             = std::make_shared<Blob>(input_desc, true);
    std::memcpy(input->GetHandle().base, logical_output.data(), logical_output.size() * sizeof(float));

    BlobDesc output_desc;
    output_desc.device_type = DEVICE_NAIVE;
    output_desc.data_type   = DATA_TYPE_FLOAT;
    output_desc.data_format = DATA_FORMAT_NCHW;
    output_desc.dims        = node.GetTopBlobShapes().front();
    auto legacy_output      = std::make_shared<Blob>(output_desc, true);
    auto direct_output      = std::make_shared<Blob>(output_desc, true);
    std::vector<std::shared_ptr<Blob>> bottoms{input};
    std::vector<std::shared_ptr<Blob>> legacy_tops{legacy_output};
    REQUIRE(bool(node.Forward(bottoms, legacy_tops)));

    RknnYolov8CandidateScratch scratch;
    RknnYolov8CandidateTiming timing;
    REQUIRE(DecodeRknnYolov8QuantizedCandidates(quantized_heads, 32, 32, 0.25f, scratch,
                                                resource.yolov8_candidate_batch.candidates, error, &timing));
    CHECK(timing.points_scanned == 21);
    CHECK(timing.points_decoded == 3);
    CHECK(resource.yolov8_candidate_batch.candidates.size() == 3);
    resource.yolov8_candidate_batch.ready = true;

    std::vector<std::shared_ptr<Blob>> direct_tops{direct_output};
    REQUIRE(bool(node.Forward(bottoms, direct_tops)));
    CHECK_FALSE(resource.yolov8_candidate_batch.ready);
    const auto count     = static_cast<size_t>(post.top_k) * 6;
    const auto* expected = static_cast<const float*>(legacy_output->GetHandle().base);
    const auto* actual   = static_cast<const float*>(direct_output->GetHandle().base);
    for (size_t index = 0; index < count; ++index)
        CHECK(actual[index] == Catch::Approx(expected[index]).margin(1e-5f));
}

TEST_CASE("RKNN YOLOv8 class-major capability preserves decode results", "[nn][rknn][yolov8][fast-output]") {
    using namespace cosmo::nn;

    SharedResource resource;
    YoloV8DecodeNode node;
    node.SetSharedResource(&resource);
    node.SetMaxBatch(1);
    YoloPost post;
    post.nms_threshold      = 0.7f;
    post.nms_detection_conf = 0.25f;
    post.top_k              = 8;
    post.input_width        = 640;
    post.input_height       = 640;
    node.LoadParam(&post);
    REQUIRE(bool(node.InferTopShapes()));

    BlobDesc input_desc;
    input_desc.device_type = DEVICE_NAIVE;
    input_desc.data_type   = DATA_TYPE_FLOAT;
    input_desc.data_format = DATA_FORMAT_NCHW;
    input_desc.dims        = {1, 7, 5};
    auto input             = std::make_shared<Blob>(input_desc, true);
    auto* values           = static_cast<float*>(input->GetHandle().base);
    std::fill(values, values + 35, 0.0f);
    for (int box = 0; box < 5; ++box) {
        values[box]      = 50.0f + box * 100.0f;
        values[5 + box]  = 50.0f + box * 100.0f;
        values[10 + box] = 20.0f;
        values[15 + box] = 20.0f;
    }
    values[20] = 0.9f;
    values[26] = 0.8f;
    values[32] = 0.7f;
    values[23] = 0.6f;
    values[28] = 0.6f;  // Tie: lower class index must continue to win.

    BlobDesc output_desc;
    output_desc.device_type = DEVICE_NAIVE;
    output_desc.data_type   = DATA_TYPE_FLOAT;
    output_desc.data_format = DATA_FORMAT_NCHW;
    output_desc.dims        = node.GetTopBlobShapes().front();
    auto legacy_output      = std::make_shared<Blob>(output_desc, true);
    auto fast_output        = std::make_shared<Blob>(output_desc, true);
    std::vector<std::shared_ptr<Blob>> bottoms{input};

    resource.prefer_yolov8_class_major_scan = false;
    std::vector<std::shared_ptr<Blob>> legacy_tops{legacy_output};
    REQUIRE(bool(node.Forward(bottoms, legacy_tops)));
    resource.prefer_yolov8_class_major_scan = true;
    std::vector<std::shared_ptr<Blob>> fast_tops{fast_output};
    REQUIRE(bool(node.Forward(bottoms, fast_tops)));

    CHECK(legacy_output->GetBlobDesc().dims == fast_output->GetBlobDesc().dims);
    const auto count = static_cast<size_t>(post.top_k) * 6;
    CHECK(std::memcmp(legacy_output->GetHandle().base, fast_output->GetHandle().base,
                      count * sizeof(float)) == 0);
}

#endif  // COSMO_NN_USE_RKNN_BACKEND
