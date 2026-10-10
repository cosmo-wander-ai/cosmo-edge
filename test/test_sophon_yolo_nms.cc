#include "catch_amalgamated.hpp"
#include "nn/device/sophon/sophon_yolo_nms.h"

#ifdef COSMO_NN_USE_HOST_BACKEND
#include <array>
#include <cmath>

#include "nn/device/cpu/cpu_yolo_decode_node.h"
#include "nn/utils/op.h"
#endif

using cosmo::nn::SophonYoloNms;
using cosmo::nn::YoloBox;
using cosmo::nn::YoloBoxVec;

TEST_CASE("Sophon YOLO NMS suppresses unequal boxes using center coordinates", "[nn][sophon][nms]") {
    // The smaller box is contained: IoU = (70 * 90) / (80 * 200) = 0.39375.
    // Treating these centers as upper-left corners instead gives about 0.1234.
    YoloBox full{100.0f, 150.0f, 80.0f, 200.0f, 0.91f, 3};
    YoloBox part{100.0f, 95.0f, 70.0f, 90.0f, 0.82f, 3};

    SECTION("higher confidence full box survives") {}

    SECTION("higher confidence partial box survives") {
        std::swap(full.confidence, part.confidence);
    }

    SECTION("translation does not change suppression") {
        full.x += 200.25f;
        full.y -= 180.5f;
        part.x += 200.25f;
        part.y -= 180.5f;
    }

    const auto expected = full.confidence > part.confidence ? full : part;
    YoloBoxVec detections{part, full};
    const auto result = SophonYoloNms(detections, 0.35f);

    REQUIRE(result.size() == 1);
    // NMS must leave the surviving center, size and metadata unchanged for the parser.
    CHECK(result[0].x == expected.x);
    CHECK(result[0].y == expected.y);
    CHECK(result[0].width == expected.width);
    CHECK(result[0].height == expected.height);
    CHECK(result[0].confidence == expected.confidence);
    CHECK(result[0].class_id == expected.class_id);
}

TEST_CASE("Sophon YOLO NMS preserves adjacent boxes of different sizes", "[nn][sophon][nms]") {
    // Actual overlap is 40 * 140, giving IoU = 5600 / 22800, about 0.2456.
    // Treating the centers as upper-left corners inflates IoU to 0.42.
    YoloBoxVec detections{{140.0f, 100.0f, 60.0f, 140.0f, 0.87f, 0},
                          {100.0f, 100.0f, 100.0f, 200.0f, 0.91f, 0}};

    const auto result = SophonYoloNms(detections, 0.35f);

    REQUIRE(result.size() == 2);
    CHECK(result[0].confidence == 0.91f);
    CHECK(result[0].x == 100.0f);
    CHECK(result[1].confidence == 0.87f);
    CHECK(result[1].x == 140.0f);
}

TEST_CASE("Sophon YOLO NMS keeps the strict IoU threshold boundary", "[nn][sophon][nms]") {
    YoloBoxVec detections{{0.0f, 0.0f, 8.0f, 8.0f, 0.9f, 0}, {0.0f, 0.0f, 4.0f, 8.0f, 0.8f, 0}};

    SECTION("IoU equal to the threshold is retained") {
        CHECK(SophonYoloNms(detections, 0.5f).size() == 2);
    }

    SECTION("IoU above the threshold is suppressed") {
        CHECK(SophonYoloNms(detections, 0.49f).size() == 1);
    }
}

TEST_CASE("Sophon YOLO NMS retains touching and separate boxes", "[nn][sophon][nms]") {
    YoloBoxVec detections{{0.0f, 12.0f, 8.0f, 8.0f, 0.7f, 0},
                          {0.0f, 0.0f, 8.0f, 8.0f, 0.9f, 0},
                          {8.0f, 0.0f, 8.0f, 8.0f, 0.8f, 0}};

    const auto result = SophonYoloNms(detections, 0.0f);

    REQUIRE(result.size() == 3);
    CHECK(result[0].confidence == 0.9f);
    CHECK(result[1].confidence == 0.8f);
    CHECK(result[2].confidence == 0.7f);
}

TEST_CASE("Sophon YOLO NMS handles empty and single candidate input", "[nn][sophon][nms]") {
    YoloBoxVec detections;

    SECTION("empty input") {
        CHECK(SophonYoloNms(detections, 0.35f).empty());
    }

    SECTION("single candidate") {
        detections.push_back({1.5f, -2.25f, 4.0f, 6.0f, 0.9f, 2});
        const auto result = SophonYoloNms(detections, 0.35f);
        REQUIRE(result.size() == 1);
        CHECK(result[0].x == 1.5f);
        CHECK(result[0].y == -2.25f);
    }
}

#ifdef COSMO_NN_USE_HOST_BACKEND
TEST_CASE("CPU YOLO decode NMS agrees with Sophon for unequal centered boxes", "[nn][cpu][nms]") {
    using namespace cosmo::nn;
    // Center-based IoU is 80 / 216 > .35. Using centers as upper-left corners
    // gives 60 / 236 < .35 and incorrectly retains both candidates.
    const auto logit = [](float probability) { return std::log(probability / (1.0f - probability)); };
    std::array<float, 6> first{0.0f, 0.0f, 0.0f, 0.0f, logit(std::sqrt(0.9f)), logit(std::sqrt(0.9f))};
    std::array<float, 6> second{logit(0.6f),           0.0f, 0.0f, 0.0f, logit(std::sqrt(0.8f)),
                                logit(std::sqrt(0.8f))};
    YoloNpuPost param;
    param.top_k              = 2;
    param.nms_threshold      = 0.35f;
    param.nms_detection_conf = 0.1f;
    param.anchors            = {{{10.0f, 10.0f}}, {{14.0f, 14.0f}}};
    param.stride             = {20.0f, 20.0f};
    CpuYoloDecodeNPUNode node;
    node.LoadParam(&param);
    REQUIRE(bool(node.InferTopShapes()));

    BlobDesc head_desc;
    head_desc.dims = {1, 1, 1, 1, 6};
    BlobHandle first_handle, second_handle;
    first_handle.base  = first.data();
    second_handle.base = second.data();
    std::vector<std::shared_ptr<Blob>> heads{std::make_shared<Blob>(head_desc, first_handle),
                                             std::make_shared<Blob>(head_desc, second_handle)};
    std::array<float, 12> output{};
    BlobDesc top_desc;
    top_desc.dims = node.GetTopBlobShapes().front();
    BlobHandle top_handle;
    top_handle.base = output.data();
    std::vector<std::shared_ptr<Blob>> tops{std::make_shared<Blob>(top_desc, top_handle)};
    REQUIRE(bool(node.Forward(heads, tops)));

    YoloBoxVec boxes{{10, 10, 10, 10, 0.9f, 0}, {14, 10, 14, 14, 0.8f, 0}};
    const auto expected = SophonYoloNms(boxes, param.nms_threshold);
    REQUIRE(expected.size() == 1);
    CHECK(output[0] == Catch::Approx(expected[0].x));
    CHECK(output[1] == Catch::Approx(expected[0].y));
    CHECK(output[2] == Catch::Approx(expected[0].width));
    CHECK(output[3] == Catch::Approx(expected[0].height));
    CHECK(output[4] == Catch::Approx(expected[0].confidence));
    CHECK(output[5] == expected[0].class_id);
    CHECK(output[10] == 0.0f);  // Suppressed output slot stays empty.
}
#endif
